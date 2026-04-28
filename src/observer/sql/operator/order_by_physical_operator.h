/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "sql/operator/physical_operator.h"

class OrderByPhysicalOperator : public PhysicalOperator
{
public:
  OrderByPhysicalOperator(vector<unique_ptr<Expression>> &&order_by_exprs, vector<bool> &&order_ascs);
  virtual ~OrderByPhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::ORDER_BY; }
  OpType               get_op_type() const override { return OpType::ORDERBY; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override;
  RC     tuple_schema(TupleSchema &schema) const override;

private:
  struct SortRow
  {
    ValueListTuple tuple;
    vector<Value>  keys;
  };

  bool less_than(const SortRow &left, const SortRow &right) const;

private:
  vector<unique_ptr<Expression>> order_by_exprs_;
  vector<bool>                   order_ascs_;
  vector<SortRow>                rows_;
  size_t                         current_ = 0;
};
