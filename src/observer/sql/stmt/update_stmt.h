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
#include "sql/stmt/stmt.h"

class Table;
class FieldMeta;
class FilterStmt;
class ParsedSqlNode;

class UpdateStmt : public Stmt
{
public:
  UpdateStmt(Table *table, const FieldMeta *field_meta, const Value &value, shared_ptr<ParsedSqlNode> value_sub_query,
      FilterStmt *filter_stmt);
  UpdateStmt(Table *table, vector<const FieldMeta *> field_metas, vector<Value> values,
      vector<shared_ptr<ParsedSqlNode>> value_sub_queries, FilterStmt *filter_stmt);
  ~UpdateStmt() override;

  StmtType type() const override { return StmtType::UPDATE; }

  Table            *table() const { return table_; }
  const FieldMeta  *field_meta() const { return field_meta_; }
  const Value      &value() const { return value_; }
  bool              value_is_sub_query() const { return value_is_sub_query_; }
  shared_ptr<ParsedSqlNode> value_sub_query() const { return value_sub_query_; }
  const vector<const FieldMeta *> &field_metas() const { return field_metas_; }
  const vector<Value> &values() const { return values_; }
  const vector<shared_ptr<ParsedSqlNode>> &value_sub_queries() const { return value_sub_queries_; }
  FilterStmt       *filter_stmt() const { return filter_stmt_; }

  static RC create(Db *db, const UpdateSqlNode &update_sql, Stmt *&stmt);

private:
  Table           *table_       = nullptr;
  const FieldMeta *field_meta_  = nullptr;
  Value            value_;
  bool             value_is_sub_query_ = false;
  shared_ptr<ParsedSqlNode> value_sub_query_;
  vector<const FieldMeta *> field_metas_;
  vector<Value> values_;
  vector<shared_ptr<ParsedSqlNode>> value_sub_queries_;
  FilterStmt      *filter_stmt_ = nullptr;
};
