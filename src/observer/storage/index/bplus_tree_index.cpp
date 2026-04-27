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
// Created by wangyunlai.wyl on 2021/5/19.
//

#include "storage/index/bplus_tree_index.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/db/db.h"

BplusTreeIndex::~BplusTreeIndex() noexcept { close(); }

RC BplusTreeIndex::create(
    Table *table, const char *file_name, const IndexMeta &index_meta, const vector<const FieldMeta *> &field_metas)
{
  if (inited_) {
    LOG_WARN("Failed to create index due to the index has been created before. file_name:%s, index:%s, field:%s",
        file_name, index_meta.name(), index_meta.field());
    return RC::RECORD_OPENNED;
  }

  RC rc = Index::init(index_meta, field_metas);
  if (OB_FAIL(rc)) {
    return rc;
  }

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  AttrType key_type = is_single_field_index() ? field_metas_.front().type() : AttrType::CHARS;
  rc = index_handler_.create(table->db()->log_handler(), bpm, file_name, key_type, key_length());
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to create index_handler, file_name:%s, index:%s, field:%s, rc:%s",
        file_name, index_meta.name(), index_meta.field(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully create index, file_name:%s, index:%s, field:%s",
    file_name, index_meta.name(), index_meta.field());
  return RC::SUCCESS;
}

RC BplusTreeIndex::open(
    Table *table, const char *file_name, const IndexMeta &index_meta, const vector<const FieldMeta *> &field_metas)
{
  if (inited_) {
    LOG_WARN("Failed to open index due to the index has been initedd before. file_name:%s, index:%s, field:%s",
        file_name, index_meta.name(), index_meta.field());
    return RC::RECORD_OPENNED;
  }

  RC rc = Index::init(index_meta, field_metas);
  if (OB_FAIL(rc)) {
    return rc;
  }

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  rc = index_handler_.open(table->db()->log_handler(), bpm, file_name);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to open index_handler, file_name:%s, index:%s, field:%s, rc:%s",
        file_name, index_meta.name(), index_meta.field(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully open index, file_name:%s, index:%s, field:%s",
    file_name, index_meta.name(), index_meta.field());
  return RC::SUCCESS;
}

RC BplusTreeIndex::close()
{
  if (inited_) {
    LOG_INFO("Begin to close index, index:%s, field:%s", index_meta_.name(), index_meta_.field());
    index_handler_.close();
    inited_ = false;
  }
  LOG_INFO("Successfully close index.");
  return RC::SUCCESS;
}

RC BplusTreeIndex::insert_entry(const char *record, const RID *rid)
{
  if (has_null_field(record)) {
    return RC::SUCCESS;
  }

  vector<char> key;
  RC rc = make_key(record, key);
  if (OB_FAIL(rc)) {
    return rc;
  }

  if (index_meta_.unique()) {
    list<RID> rids;
    int search_len = key_length();
    vector<char> search_key;
    if (is_single_field_index() && field_metas_.front().type() == AttrType::CHARS) {
      search_len = 0;
      while (search_len < static_cast<int>(key.size()) && key[search_len] != '\0') {
        search_len++;
      }
    } else if (!is_single_field_index()) {
      search_key = key;
      search_key.push_back('\0');
    }
    rc = index_handler_.get_entry(search_key.empty() ? key.data() : search_key.data(), search_len, rids);
    if (OB_FAIL(rc)) {
      return rc;
    }
    for (const RID &existing_rid : rids) {
      if (RID::compare(&existing_rid, rid) != 0) {
        return RC::RECORD_DUPLICATE_KEY;
      }
    }
  }

  return index_handler_.insert_entry(key.data(), rid);
}

RC BplusTreeIndex::delete_entry(const char *record, const RID *rid)
{
  if (has_null_field(record)) {
    return RC::SUCCESS;
  }

  vector<char> key;
  RC rc = make_key(record, key);
  if (OB_FAIL(rc)) {
    return rc;
  }
  return index_handler_.delete_entry(key.data(), rid);
}

int BplusTreeIndex::key_length() const
{
  if (is_single_field_index()) {
    return field_metas_.front().len();
  }

  int raw_len = 0;
  for (const FieldMeta &field_meta : field_metas_) {
    raw_len += field_meta.len();
  }
  return raw_len * 2;
}

bool BplusTreeIndex::has_null_field(const char *record) const
{
  if (table_ == nullptr || record == nullptr || table_->table_meta().null_bitmap_size() <= 0) {
    return false;
  }

  const TableMeta &table_meta = table_->table_meta();
  const char *bitmap = record + table_meta.null_bitmap_offset();
  for (const FieldMeta &field_meta : field_metas_) {
    if (!field_meta.visible()) {
      continue;
    }
    const int byte_idx = field_meta.field_id() / 8;
    const int bit_idx  = field_meta.field_id() % 8;
    const unsigned char mask = static_cast<unsigned char>(1U << bit_idx);
    if ((static_cast<unsigned char>(bitmap[byte_idx]) & mask) != 0) {
      return true;
    }
  }
  return false;
}

RC BplusTreeIndex::make_key(const char *record, vector<char> &key) const
{
  if (record == nullptr || field_metas_.empty()) {
    return RC::INVALID_ARGUMENT;
  }

  if (is_single_field_index()) {
    const FieldMeta &field_meta = field_metas_.front();
    key.assign(record + field_meta.offset(), record + field_meta.offset() + field_meta.len());
    return RC::SUCCESS;
  }

  static constexpr char HEX[] = "0123456789ABCDEF";
  key.clear();
  key.reserve(key_length());
  for (const FieldMeta &field_meta : field_metas_) {
    const unsigned char *data = reinterpret_cast<const unsigned char *>(record + field_meta.offset());
    for (int i = 0; i < field_meta.len(); i++) {
      key.push_back(HEX[data[i] >> 4]);
      key.push_back(HEX[data[i] & 0x0F]);
    }
  }
  key.push_back('\0');
  return RC::SUCCESS;
}

IndexScanner *BplusTreeIndex::create_scanner(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  BplusTreeIndexScanner *index_scanner = new BplusTreeIndexScanner(index_handler_);
  RC rc = index_scanner->open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open index scanner. rc=%d:%s", rc, strrc(rc));
    delete index_scanner;
    return nullptr;
  }
  return index_scanner;
}

RC BplusTreeIndex::sync() { return index_handler_.sync(); }

////////////////////////////////////////////////////////////////////////////////
BplusTreeIndexScanner::BplusTreeIndexScanner(BplusTreeHandler &tree_handler) : tree_scanner_(tree_handler) {}

BplusTreeIndexScanner::~BplusTreeIndexScanner() noexcept { tree_scanner_.close(); }

RC BplusTreeIndexScanner::open(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  return tree_scanner_.open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
}

RC BplusTreeIndexScanner::next_entry(RID *rid) { return tree_scanner_.next_entry(*rid); }

RC BplusTreeIndexScanner::destroy()
{
  delete this;
  return RC::SUCCESS;
}
