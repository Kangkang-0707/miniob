/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by WangYunlai on 2022/6/7.
//

#pragma once

#include "common/sys/rc.h"
#include "sql/expr/tuple.h"
#include "sql/operator/operator_node.h"

class Record;
class Chunk;
class TupleCellSpec;
class Trx;

enum class PhysicalOperatorType
{
  TABLE_SCAN,
  TABLE_SCAN_VEC,
  INDEX_SCAN,
  NESTED_LOOP_JOIN,
  HASH_JOIN,
  EXPLAIN,
  PREDICATE,
  PREDICATE_VEC,
  PROJECT,
  PROJECT_VEC,
  CALC,
  STRING_LIST,
  DELETE,
  INSERT,
  UPDATE,
  SCALAR_GROUP_BY,
  HASH_GROUP_BY,
  GROUP_BY_VEC,
  AGGREGATE_VEC,
  EXPR_VEC,
};

class PhysicalOperator : public OperatorNode
{
public:
  PhysicalOperator() = default;
  virtual ~PhysicalOperator() = default;

  virtual string name() const;
  virtual string param() const;

  bool is_physical() const override { return true; }
  bool is_logical() const override { return false; }

  virtual PhysicalOperatorType type() const = 0;

  virtual RC open(Trx *trx) = 0;
  virtual RC next() { return RC::UNIMPLEMENTED; }
  virtual RC next(Chunk &chunk) { return RC::UNIMPLEMENTED; }
  virtual RC close() = 0;

  virtual Tuple *current_tuple() { return nullptr; }
  virtual RC     tuple_schema(TupleSchema &schema) const { return RC::UNIMPLEMENTED; }

  void add_child(unique_ptr<PhysicalOperator> oper) { children_.emplace_back(std::move(oper)); }
  vector<unique_ptr<PhysicalOperator>> &children() { return children_; }

protected:
  vector<unique_ptr<PhysicalOperator>> children_;
};
