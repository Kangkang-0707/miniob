/* Copyright (c) 2021 Xie Meiyi(xiemeiyi@hust.edu.cn) and OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Meiyi & Wangyunlai on 2021/5/12.
//

#pragma once

#include "storage/table/table_meta.h"
#include "storage/table/table_engine.h"
#include "storage/common/chunk.h"
#include "storage/record/lob_handler.h"
#include "common/types.h"
#include "common/lang/span.h"
#include "common/lang/functional.h"

struct RID;
class Record;
class DiskBufferPool;
class RecordFileHandler;
class RecordScanner;
class ChunkFileScanner;
class ConditionFilter;
class DefaultConditionFilter;
class Index;
class IndexScanner;
class RecordDeleter;
class Trx;
class Db;

/**
 * @brief 琛?
 *
 */
class Table
{
public:
  Table() = default;
  ~Table();

  // TODO: use TableEngine replace Table
  friend class TableEngine;
  friend class HeapTableEngine;

  /**
   * 鍒涘缓涓€涓〃
   * @param path 鍏冩暟鎹繚瀛樼殑鏂囦欢(瀹屾暣璺緞)
   * @param name 琛ㄥ悕
   * @param base_dir 琛ㄦ暟鎹瓨鏀剧殑璺緞
   * @param attribute_count 瀛楁涓暟
   * @param attributes 瀛楁
   */
  RC create(Db *db, int32_t table_id, const char *path, const char *name, const char *base_dir,
      span<const AttrInfoSqlNode> attributes, const vector<string> &primary_keys, StorageFormat storage_format,
      StorageEngine storage_engine);

  /**
   * 鎵撳紑涓€涓〃
   * @param meta_file 淇濆瓨琛ㄥ厓鏁版嵁鐨勬枃浠跺畬鏁磋矾寰?
   * @param base_dir 琛ㄦ墍鍦ㄧ殑鏂囦欢澶癸紝琛ㄨ褰曟暟鎹枃浠躲€佺储寮曟暟鎹枃浠跺瓨鏀句綅缃?
   */
  RC open(Db *db, const char *meta_file, const char *base_dir);

  /**
   * @brief 鏍规嵁缁欏畾鐨勫瓧娈电敓鎴愪竴涓褰?琛?
   * @details 閫氬父鏄敱鐢ㄦ埛浼犺繃鏉ョ殑瀛楁锛屾寜鐓chema淇℃伅缁勮鎴愪竴涓猺ecord銆?
   * @param value_num 瀛楁鐨勪釜鏁?
   * @param values    姣忎釜瀛楁鐨勫€?
   * @param record    鐢熸垚鐨勮褰曟暟鎹?
   */
  RC make_record(int value_num, const Value *values, Record &record);

  /**
   * @brief 鍦ㄥ綋鍓嶇殑琛ㄤ腑鎻掑叆涓€鏉¤褰?
   * @details 鍦ㄨ〃鏂囦欢鍜岀储寮曚腑鎻掑叆鍏宠仈鏁版嵁銆傝繖閲屽彧绠″湪琛ㄤ腑鎻掑叆鏁版嵁锛屼笉鍏冲績浜嬪姟鐩稿叧鎿嶄綔銆?
   * @param record[in/out] 浼犲叆鐨勬暟鎹寘鍚叿浣撶殑鏁版嵁锛屾彃鍏ユ垚鍔熶細閫氳繃姝ゅ瓧娈佃繑鍥濺ID
   */
  RC insert_record(Record &record);

  RC insert_chunk(const Chunk &chunk);
  RC delete_record(const Record &record);

  RC insert_record_with_trx(Record &record, Trx *trx);
  RC delete_record_with_trx(const Record &record, Trx *trx);
  RC update_record_with_trx(const Record &old_record, const Record &new_record, Trx *trx);
  RC get_record(const RID &rid, Record &record);

  // TODO refactor
  RC create_index(Trx *trx, const vector<const FieldMeta *> &field_metas, const char *index_name, bool unique);

  RC get_record_scanner(RecordScanner *&scanner, Trx *trx, ReadWriteMode mode);

  RC get_chunk_scanner(ChunkFileScanner &scanner, Trx *trx, ReadWriteMode mode);

  /**
   * @brief 鍙互鍦ㄩ〉闈㈤攣淇濇姢鐨勬儏鍐典笅璁块棶璁板綍
   * @details 褰撳墠鏄湪浜嬪姟涓闂褰曪紝涓轰簡鎻愪緵涓€涓€滃師瀛愭€р€濈殑璁块棶妯″紡
   * @param rid
   * @param visitor
   * @return RC
   */
  RC visit_record(const RID &rid, function<bool(Record &)> visitor);

public:
  int32_t     table_id() const { return table_meta_.table_id(); }
  const char *name() const;

  Db *db() const { return db_; }

  const TableMeta &table_meta() const;

  LobFileHandler *lob_handler() const { return lob_handler_; }

  RC sync();

private:
  RC set_value_to_record(char *record_data, const Value &value, const FieldMeta *field);
  void set_field_null(char *record_data, int field_id, bool is_null) const;
  bool field_is_null(const char *record_data, int field_id) const;

private:
  // RC init_record_handler(const char *base_dir);

public:
  Index *find_index(const char *index_name) const;
  Index *find_index_by_field(const char *field_name) const;

private:
  Db       *db_ = nullptr;
  TableMeta table_meta_;
  // DiskBufferPool    *data_buffer_pool_ = nullptr;  /// 鏁版嵁鏂囦欢鍏宠仈鐨刡uffer pool
  // RecordFileHandler *record_handler_   = nullptr;  /// 璁板綍鎿嶄綔
  // vector<Index *>    indexes_;
  unique_ptr<TableEngine> engine_      = nullptr;
  LobFileHandler         *lob_handler_ = nullptr;
};
