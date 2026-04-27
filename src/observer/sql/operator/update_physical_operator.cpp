/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/update_physical_operator.h"

#include <algorithm>
#include <cstring>

#include "common/log/log.h"
#include "session/session.h"
#include "sql/expr/tuple.h"
#include "sql/optimizer/logical_plan_generator.h"
#include "sql/optimizer/physical_plan_generator.h"
#include "sql/optimizer/rewriter.h"
#include "sql/parser/parse_defs.h"
#include "sql/stmt/select_stmt.h"
#include "storage/db/db.h"
#include "storage/field/field_meta.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"

namespace {

RC execute_sub_query(Db *db, shared_ptr<ParsedSqlNode> sub_query_sql, vector<ValueListTuple> &rows)
{
  rows.clear();
  if (sub_query_sql == nullptr || sub_query_sql->flag != SCF_SELECT) {
    return RC::INVALID_ARGUMENT;
  }

  Stmt *sub_stmt_raw = nullptr;
  RC    rc           = SelectStmt::create(db, sub_query_sql->selection, sub_stmt_raw);
  if (OB_FAIL(rc)) {
    return rc;
  }

  unique_ptr<Stmt> sub_stmt(sub_stmt_raw);
  auto            *select_stmt = static_cast<SelectStmt *>(sub_stmt.get());

  LogicalPlanGenerator logical_plan_generator;
  unique_ptr<LogicalOperator> logical_operator;
  rc = logical_plan_generator.create(select_stmt, logical_operator);
  if (OB_FAIL(rc)) {
    return rc;
  }

  Rewriter rewriter;
  bool     change_made = false;
  do {
    change_made = false;
    rc          = rewriter.rewrite(logical_operator, change_made);
    if (OB_FAIL(rc)) {
      return rc;
    }
  } while (change_made);

  Session session;
  session.set_current_db(db->name());
  Session *old_session = Session::current_session();
  Session::set_current_session(&session);

  PhysicalPlanGenerator physical_plan_generator;
  unique_ptr<PhysicalOperator> physical_operator;
  rc = physical_plan_generator.create(*logical_operator, physical_operator, &session);
  if (OB_FAIL(rc)) {
    session.destroy_trx();
    Session::set_current_session(old_session);
    return rc;
  }

  rc = physical_operator->open(session.current_trx());
  if (OB_FAIL(rc)) {
    session.destroy_trx();
    Session::set_current_session(old_session);
    return rc;
  }

  while (OB_SUCC(rc = physical_operator->next())) {
    Tuple *tuple = physical_operator->current_tuple();
    if (tuple == nullptr) {
      rc = RC::INTERNAL;
      break;
    }

    ValueListTuple row;
    rc = ValueListTuple::make(*tuple, row);
    if (OB_FAIL(rc)) {
      break;
    }
    rows.emplace_back(std::move(row));
  }

  physical_operator->close();
  session.destroy_trx();
  Session::set_current_session(old_session);

  if (rc == RC::RECORD_EOF) {
    return RC::SUCCESS;
  }
  return rc;
}

RC extract_single_value(Db *db, shared_ptr<ParsedSqlNode> sub_query_sql, Value &value)
{
  if (sub_query_sql == nullptr || sub_query_sql->flag != SCF_SELECT ||
      sub_query_sql->selection.expressions.size() != 1) {
    return RC::INVALID_ARGUMENT;
  }

  vector<ValueListTuple> rows;
  RC rc = execute_sub_query(db, sub_query_sql, rows);
  if (OB_FAIL(rc)) {
    return rc;
  }

  if (rows.empty()) {
    value.set_null();
    return RC::SUCCESS;
  }
  if (rows.size() > 1) {
    return RC::INVALID_ARGUMENT;
  }
  if (rows[0].cell_num() != 1) {
    return RC::INVALID_ARGUMENT;
  }

  return rows[0].cell_at(0, value);
}

} // namespace

UpdatePhysicalOperator::UpdatePhysicalOperator(
    Table *table, const FieldMeta *field_meta, const Value &value, shared_ptr<ParsedSqlNode> value_sub_query)
    : table_(table), field_meta_(field_meta), value_(value), value_sub_query_(std::move(value_sub_query))
{}

RC UpdatePhysicalOperator::open(Trx *trx)
{
  trx_ = trx;
  records_.clear();

  if (children_.empty()) {
    return RC::SUCCESS;
  }

  unique_ptr<PhysicalOperator> &child = children_[0];
  RC rc = child->open(trx);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  while (OB_SUCC(rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (tuple == nullptr) {
      LOG_WARN("failed to get current tuple");
      child->close();
      return RC::INTERNAL;
    }

    RowTuple *row_tuple = static_cast<RowTuple *>(tuple);
    Record    record;
    rc = record.copy_data(row_tuple->record().data(), row_tuple->record().len());
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to copy record: %s", strrc(rc));
      child->close();
      return rc;
    }
    record.set_rid(row_tuple->record().rid());
    records_.emplace_back(std::move(record));
  }

  child->close();
  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;
  }
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to fetch records for update: %s", strrc(rc));
    return rc;
  }

  Value resolved_value;
  rc = resolve_value(resolved_value);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to resolve update value: %s", strrc(rc));
    return rc;
  }

  value_ = resolved_value;

  vector<pair<Record, Record>> applied_records;
  for (Record &old_record : records_) {
    Record new_record(old_record);
    rc = apply_value(new_record);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to apply update value: %s", strrc(rc));
      for (auto iter = applied_records.rbegin(); iter != applied_records.rend(); ++iter) {
        RC rc2 = trx_->update_record(table_, iter->second, iter->first);
        if (OB_FAIL(rc2)) {
          LOG_PANIC("failed to rollback updated record: %s", strrc(rc2));
        }
      }
      return rc;
    }

    rc = trx_->update_record(table_, old_record, new_record);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to update record: %s", strrc(rc));
      for (auto iter = applied_records.rbegin(); iter != applied_records.rend(); ++iter) {
        RC rc2 = trx_->update_record(table_, iter->second, iter->first);
        if (OB_FAIL(rc2)) {
          LOG_PANIC("failed to rollback updated record: %s", strrc(rc2));
        }
      }
      return rc;
    }
    applied_records.emplace_back(old_record, new_record);
  }

  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::next()
{
  return RC::RECORD_EOF;
}

RC UpdatePhysicalOperator::close()
{
  records_.clear();
  trx_ = nullptr;
  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::apply_value(Record &record) const
{
  if (record.data() == nullptr || field_meta_ == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  char *bitmap = nullptr;
  int byte_idx = 0;
  unsigned char mask = 0;
  if (table_->table_meta().null_bitmap_size() > 0) {
    bitmap = record.data() + table_->table_meta().null_bitmap_offset();
    byte_idx = field_meta_->field_id() / 8;
    const int bit_idx = field_meta_->field_id() % 8;
    mask = static_cast<unsigned char>(1U << bit_idx);
  }

  if (value_.is_null()) {
    if (!field_meta_->nullable()) {
      return RC::INVALID_ARGUMENT;
    }
    if (bitmap != nullptr) {
      bitmap[byte_idx] = static_cast<char>(static_cast<unsigned char>(bitmap[byte_idx]) | mask);
    }
    memset(record.data() + field_meta_->offset(), 0, field_meta_->len());
    return RC::SUCCESS;
  }

  if (bitmap != nullptr) {
    bitmap[byte_idx] = static_cast<char>(static_cast<unsigned char>(bitmap[byte_idx]) & ~mask);
  }

  char       *target   = record.data() + field_meta_->offset();
  const size_t field_len = field_meta_->len();
  size_t       copy_len  = field_len;

  if (field_meta_->type() == AttrType::CHARS) {
    memset(target, 0, field_len);
    copy_len = std::min(field_len, static_cast<size_t>(value_.length() + 1));
  }

  memcpy(target, value_.data(), copy_len);
  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::resolve_value(Value &value) const
{
  if (value_sub_query_ == nullptr) {
    value = value_;
    return RC::SUCCESS;
  }

  RC rc = extract_single_value(table_->db(), value_sub_query_, value);
  if (OB_FAIL(rc)) {
    return rc;
  }

  if (value.is_null()) {
    if (!field_meta_->nullable()) {
      return RC::INVALID_ARGUMENT;
    }
    value.set_null(field_meta_->type());
    return RC::SUCCESS;
  }

  if (value.attr_type() == field_meta_->type()) {
    return RC::SUCCESS;
  }

  Value cast_value;
  rc = Value::cast_to(value, field_meta_->type(), cast_value);
  if (OB_FAIL(rc)) {
    return rc;
  }
  value = cast_value;
  return RC::SUCCESS;
}
