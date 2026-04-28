/* Copyright (c) OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/update_logical_operator.h"

UpdateLogicalOperator::UpdateLogicalOperator(
    Table *table, const FieldMeta *field_meta, const Value &value, shared_ptr<ParsedSqlNode> value_sub_query)
    : table_(table), field_meta_(field_meta), value_(value), value_sub_query_(std::move(value_sub_query))
{
  field_metas_.emplace_back(field_meta_);
  values_.emplace_back(value_);
  value_sub_queries_.emplace_back(value_sub_query_);
}

UpdateLogicalOperator::UpdateLogicalOperator(Table *table, vector<const FieldMeta *> field_metas, vector<Value> values,
    vector<shared_ptr<ParsedSqlNode>> value_sub_queries)
    : table_(table),
      field_meta_(field_metas.empty() ? nullptr : field_metas.front()),
      value_(values.empty() ? Value() : values.front()),
      value_sub_query_(value_sub_queries.empty() ? nullptr : value_sub_queries.front()),
      field_metas_(std::move(field_metas)),
      values_(std::move(values)),
      value_sub_queries_(std::move(value_sub_queries))
{}
