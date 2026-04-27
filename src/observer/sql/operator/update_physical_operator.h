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

#include "common/value.h"
#include "common/lang/memory.h"
#include "sql/operator/physical_operator.h"

class Table;
class Trx;
class FieldMeta;
class ParsedSqlNode;

class UpdatePhysicalOperator : public PhysicalOperator
{
public:
  UpdatePhysicalOperator(
      Table *table, const FieldMeta *field_meta, const Value &value, shared_ptr<ParsedSqlNode> value_sub_query);
  ~UpdatePhysicalOperator() override = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::UPDATE; }
  OpType               get_op_type() const override { return OpType::UPDATE; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override { return nullptr; }

private:
  RC apply_value(Record &record) const;
  RC resolve_value(Value &value) const;

private:
  Table              *table_      = nullptr;
  const FieldMeta    *field_meta_ = nullptr;
  Value               value_;
  shared_ptr<ParsedSqlNode> value_sub_query_;
  Trx                *trx_        = nullptr;
  vector<Record>      records_;
};
