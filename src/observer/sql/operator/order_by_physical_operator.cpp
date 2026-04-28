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
  current_ = 0;

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

    SortRow row;
    row.keys.reserve(order_by_exprs_.size());
    for (const unique_ptr<Expression> &expression : order_by_exprs_) {
      Value value;
      rc = expression->get_value(*child_tuple, value);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to evaluate order by expression. rc=%s", strrc(rc));
        return rc;
      }
      row.keys.emplace_back(std::move(value));
    }

    rc = ValueListTuple::make(*child_tuple, row.tuple);
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

  stable_sort(rows_.begin(), rows_.end(), [this](const SortRow &left, const SortRow &right) {
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
  current_ = 0;

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
  return &rows_[current_ - 1].tuple;
}

RC OrderByPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  if (children_.empty()) {
    return RC::INTERNAL;
  }
  return children_[0]->tuple_schema(schema);
}

bool OrderByPhysicalOperator::less_than(const SortRow &left, const SortRow &right) const
{
  const size_t key_num = min(left.keys.size(), right.keys.size());
  for (size_t i = 0; i < key_num; ++i) {
    const int cmp = left.keys[i].compare(right.keys[i]);
    if (cmp == 0) {
      continue;
    }

    const bool asc = i >= order_ascs_.size() ? true : order_ascs_[i];
    return asc ? cmp < 0 : cmp > 0;
  }

  return false;
}
