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
#include "sql/expr/tuple.h"
#include "storage/field/field_meta.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"

UpdatePhysicalOperator::UpdatePhysicalOperator(Table *table, const FieldMeta *field_meta, const Value &value)
    : table_(table), field_meta_(field_meta), value_(value)
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

  for (Record &old_record : records_) {
    Record new_record(old_record);
    rc = apply_value(new_record);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to apply update value: %s", strrc(rc));
      return rc;
    }

    rc = trx_->update_record(table_, old_record, new_record);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to update record: %s", strrc(rc));
      return rc;
    }
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
