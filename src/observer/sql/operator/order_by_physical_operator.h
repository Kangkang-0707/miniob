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
  class StoredTuple : public Tuple
  {
  public:
    void set_specs(const vector<TupleCellSpec> *specs) { specs_ = specs; }
    void add_cell(Value &&value) { cells_.emplace_back(std::move(value)); }

    int cell_num() const override { return static_cast<int>(cells_.size()); }
    RC  cell_at(int index, Value &cell) const override;
    RC  spec_at(int index, TupleCellSpec &spec) const override;
    RC  find_cell(const TupleCellSpec &spec, Value &cell) const override;

    const Value &cell(int index) const { return cells_[index]; }

  private:
    vector<Value>                cells_;
    const vector<TupleCellSpec> *specs_ = nullptr;
  };

  bool less_than(const StoredTuple &left, const StoredTuple &right) const;
  RC   make_stored_tuple(const Tuple &tuple, StoredTuple &stored_tuple);
  RC   init_schema_and_order_indexes(const Tuple &tuple);

private:
  vector<unique_ptr<Expression>> order_by_exprs_;
  vector<bool>                   order_ascs_;
  vector<int>                    order_cell_indexes_;
  vector<TupleCellSpec>          specs_;
  vector<StoredTuple>            rows_;
  size_t                         current_ = 0;
  bool                           schema_inited_ = false;
};
