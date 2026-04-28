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
// Created by Wangyunlai on 2022/5/22.
//

#include "sql/stmt/update_stmt.h"

#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "storage/table/table_meta.h"

UpdateStmt::UpdateStmt(
    Table *table, const FieldMeta *field_meta, const Value &value, shared_ptr<ParsedSqlNode> value_sub_query, FilterStmt *filter_stmt)
    : table_(table), field_meta_(field_meta), value_(value), value_is_sub_query_(value_sub_query != nullptr),
      value_sub_query_(std::move(value_sub_query)), filter_stmt_(filter_stmt)
{
  field_metas_.emplace_back(field_meta_);
  values_.emplace_back(value_);
  value_sub_queries_.emplace_back(value_sub_query_);
}

UpdateStmt::UpdateStmt(Table *table, vector<const FieldMeta *> field_metas, vector<Value> values,
    vector<shared_ptr<ParsedSqlNode>> value_sub_queries, FilterStmt *filter_stmt)
    : table_(table),
      field_meta_(field_metas.empty() ? nullptr : field_metas.front()),
      value_(values.empty() ? Value() : values.front()),
      value_is_sub_query_(!value_sub_queries.empty() && value_sub_queries.front() != nullptr),
      value_sub_query_(value_sub_queries.empty() ? nullptr : value_sub_queries.front()),
      field_metas_(std::move(field_metas)),
      values_(std::move(values)),
      value_sub_queries_(std::move(value_sub_queries)),
      filter_stmt_(filter_stmt)
{}

UpdateStmt::~UpdateStmt()
{
  if (filter_stmt_ != nullptr) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }
}

RC UpdateStmt::create(Db *db, const UpdateSqlNode &update, Stmt *&stmt)
{
  stmt = nullptr;

  const char *table_name = update.relation_name.c_str();
  if (db == nullptr || table_name == nullptr) {
    LOG_WARN("invalid argument. db=%p, table_name=%p", db, table_name);
    return RC::INVALID_ARGUMENT;
  }

  Table *table = db->find_table(table_name);
  if (table == nullptr) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  vector<UpdateValueSqlNode> update_values_storage;
  if (update.update_values.empty()) {
    UpdateValueSqlNode update_value;
    update_value.attribute_name      = update.attribute_name;
    update_value.value               = update.value;
    update_value.value_is_sub_query  = update.value_is_sub_query;
    update_value.value_sub_query     = update.value_sub_query;
    update_values_storage.emplace_back(std::move(update_value));
  }
  const vector<UpdateValueSqlNode> &update_values = update.update_values.empty() ? update_values_storage : update.update_values;

  vector<const FieldMeta *> field_metas;
  vector<Value> values;
  vector<shared_ptr<ParsedSqlNode>> value_sub_queries;
  field_metas.reserve(update_values.size());
  values.reserve(update_values.size());
  value_sub_queries.reserve(update_values.size());

  RC rc = RC::SUCCESS;
  for (const UpdateValueSqlNode &update_value : update_values) {
    const FieldMeta *field_meta = table->table_meta().field(update_value.attribute_name.c_str());
    if (field_meta == nullptr) {
      LOG_WARN("no such field. table=%s, field=%s", table_name, update_value.attribute_name.c_str());
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }

    Value value;
    shared_ptr<ParsedSqlNode> value_sub_query;
    if (update_value.value_is_sub_query) {
      value_sub_query = update_value.value_sub_query;
    } else if (update_value.value.is_null()) {
      if (!field_meta->nullable()) {
        LOG_WARN("field does not allow null. table=%s, field=%s", table_name, update_value.attribute_name.c_str());
        return RC::INVALID_ARGUMENT;
      }
      value.set_null(field_meta->type());
    } else if (update_value.value.attr_type() == field_meta->type()) {
      value.set_value(update_value.value);
    } else {
      rc = Value::cast_to(update_value.value, field_meta->type(), value);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to cast update value. table=%s, field=%s, rc=%s",
            table_name,
            update_value.attribute_name.c_str(),
            strrc(rc));
        return rc;
      }
    }

    field_metas.emplace_back(field_meta);
    values.emplace_back(value);
    value_sub_queries.emplace_back(std::move(value_sub_query));
  }

  unordered_map<string, Table *> table_map;
  table_map.insert(pair<string, Table *>(string(table_name), table));

  FilterStmt *filter_stmt = nullptr;
  rc = FilterStmt::create(
      db, table, &table_map, update.conditions.data(), static_cast<int>(update.conditions.size()), filter_stmt);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create filter statement. rc=%s", strrc(rc));
    return rc;
  }

  stmt = new UpdateStmt(table, std::move(field_metas), std::move(values), std::move(value_sub_queries), filter_stmt);
  return RC::SUCCESS;
}
