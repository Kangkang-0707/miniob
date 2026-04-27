/* Copyright (c) OceanBase and/or its affiliates. All rights reserved.
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
#include "sql/operator/logical_operator.h"

class Table;
class FieldMeta;
class ParsedSqlNode;

class UpdateLogicalOperator : public LogicalOperator
{
public:
  UpdateLogicalOperator(
      Table *table, const FieldMeta *field_meta, const Value &value, shared_ptr<ParsedSqlNode> value_sub_query);
  ~UpdateLogicalOperator() override = default;

  LogicalOperatorType type() const override { return LogicalOperatorType::UPDATE; }
  OpType              get_op_type() const override { return OpType::LOGICALUPDATE; }

  Table              *table() const { return table_; }
  const FieldMeta    *field_meta() const { return field_meta_; }
  const Value        &value() const { return value_; }
  shared_ptr<ParsedSqlNode> value_sub_query() const { return value_sub_query_; }

private:
  Table           *table_      = nullptr;
  const FieldMeta *field_meta_ = nullptr;
  Value            value_;
  shared_ptr<ParsedSqlNode> value_sub_query_;
};
