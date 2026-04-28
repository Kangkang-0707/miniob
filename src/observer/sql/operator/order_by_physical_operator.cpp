/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/order_by_physical_operator.h"

#include "common/lang/algorithm.h"
#include "common/log/log.h"

using namespace std;

OrderByPhysicalOperator::OrderByPhysicalOperator(
    vector<unique_ptr<Expression>> &&order_by_exprs, vector<bool> &&order_ascs)
    : order_by_exprs_(std::move(order_by_exprs)), order_ascs_(std::move(order_ascs))
{}

RC OrderByPhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 1) {
    LOG_WARN("order by operator must have one child, but got %d", static_cast<int>(children_.size()));
    return RC::INTERNAL;
  }

  rows_.clear();
  specs_.clear();
  order_cell_indexes_.clear();
  current_ = 0;
  schema_inited_ = false;

  PhysicalOperator *child = children_[0].get();
  RC                rc    = child->open(trx);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to open child operator. rc=%s", strrc(rc));
    return rc;
  }

  while (OB_SUCC(rc = child->next())) {
    Tuple *child_tuple = child->current_tuple();
    if (child_tuple == nullptr) {
      LOG_WARN("failed to get tuple from order by child");
      return RC::INTERNAL;
    }

    if (!schema_inited_) {
      rc = init_schema_and_order_indexes(*child_tuple);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to init order by schema. rc=%s", strrc(rc));
        return rc;
      }
    }

    StoredTuple row;
    rc = make_stored_tuple(*child_tuple, row);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to materialize order by tuple. rc=%s", strrc(rc));
      return rc;
    }

    rows_.emplace_back(std::move(row));
  }

  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;
  }
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to get next tuple from order by child. rc=%s", strrc(rc));
    return rc;
  }

  stable_sort(rows_.begin(), rows_.end(), [this](const StoredTuple &left, const StoredTuple &right) {
    return less_than(left, right);
  });

  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::next()
{
  if (current_ >= rows_.size()) {
    return RC::RECORD_EOF;
  }

  ++current_;
  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::close()
{
  rows_.clear();
  specs_.clear();
  order_cell_indexes_.clear();
  current_ = 0;
  schema_inited_ = false;

  if (!children_.empty()) {
    children_[0]->close();
  }
  return RC::SUCCESS;
}

Tuple *OrderByPhysicalOperator::current_tuple()
{
  if (current_ == 0 || current_ > rows_.size()) {
    return nullptr;
  }
  return &rows_[current_ - 1];
}

RC OrderByPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  if (children_.empty()) {
    return RC::INTERNAL;
  }
  return children_[0]->tuple_schema(schema);
}

bool OrderByPhysicalOperator::less_than(const StoredTuple &left, const StoredTuple &right) const
{
  const size_t key_num = order_cell_indexes_.size();
  for (size_t i = 0; i < key_num; ++i) {
    const int cell_index = order_cell_indexes_[i];
    const int cmp        = left.cell(cell_index).compare(right.cell(cell_index));
    if (cmp == 0) {
      continue;
    }

    const bool asc = i >= order_ascs_.size() ? true : order_ascs_[i];
    return asc ? cmp < 0 : cmp > 0;
  }

  return false;
}

RC OrderByPhysicalOperator::make_stored_tuple(const Tuple &tuple, StoredTuple &stored_tuple)
{
  stored_tuple.set_specs(&specs_);

  const int cell_num = tuple.cell_num();
  for (int i = 0; i < cell_num; ++i) {
    Value value;
    RC    rc = tuple.cell_at(i, value);
    if (OB_FAIL(rc)) {
      return rc;
    }
    stored_tuple.add_cell(std::move(value));
  }

  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::init_schema_and_order_indexes(const Tuple &tuple)
{
  const int cell_num = tuple.cell_num();
  specs_.reserve(cell_num);
  for (int i = 0; i < cell_num; ++i) {
    TupleCellSpec spec;
    RC            rc = tuple.spec_at(i, spec);
    if (OB_FAIL(rc)) {
      return rc;
    }
    specs_.emplace_back(std::move(spec));
  }

  order_cell_indexes_.reserve(order_by_exprs_.size());
  for (const unique_ptr<Expression> &expression : order_by_exprs_) {
    if (expression->type() != ExprType::FIELD) {
      return RC::INVALID_ARGUMENT;
    }

    const FieldExpr    *field_expr = static_cast<const FieldExpr *>(expression.get());
    const TupleCellSpec order_spec(field_expr->table_name(), field_expr->field_name());
    int                found_index = -1;
    for (int i = 0; i < static_cast<int>(specs_.size()); ++i) {
      if (specs_[i].equals(order_spec)) {
        found_index = i;
        break;
      }
    }

    if (found_index < 0) {
      return RC::SCHEMA_FIELD_MISSING;
    }
    order_cell_indexes_.push_back(found_index);
  }

  schema_inited_ = true;
  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::StoredTuple::cell_at(int index, Value &cell) const
{
  if (index < 0 || index >= cell_num()) {
    return RC::NOTFOUND;
  }

  cell = cells_[index];
  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::StoredTuple::spec_at(int index, TupleCellSpec &spec) const
{
  if (specs_ == nullptr || index < 0 || index >= static_cast<int>(specs_->size())) {
    return RC::NOTFOUND;
  }

  spec = (*specs_)[index];
  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::StoredTuple::find_cell(const TupleCellSpec &spec, Value &cell) const
{
  if (specs_ == nullptr || specs_->size() != cells_.size()) {
    return RC::INTERNAL;
  }

  for (int i = 0; i < static_cast<int>(specs_->size()); ++i) {
    if ((*specs_)[i].equals(spec)) {
      cell = cells_[i];
      return RC::SUCCESS;
    }
  }

  return RC::NOTFOUND;
}
