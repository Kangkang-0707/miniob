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
// Created by Wangyunlai 2023/6/27
//

#pragma once

#include "common/lang/string.h"
#include "common/lang/memory.h"
#include "common/type/attr_type.h"
#include "common/type/data_type.h"
#include "common/type/string_t.h"

/**
 * @brief 灞炴€х殑鍊?
 * @ingroup DataType
 * @details 涓嶥ataType锛屽氨鏄暟鎹被鍨嬶紝閰嶅瀹屾垚鍚勭绠楁湳杩愮畻銆佹瘮杈冦€佺被鍨嬭浆鎹㈢瓑鎿嶄綔銆傝繖閲屽悓鏃惰褰曚簡鏁版嵁鐨勫€间笌绫诲瀷銆?
 * 褰撻渶瑕佸鍊煎仛杩愮畻鏃讹紝寤鸿浣跨敤绫讳技 Value::add 鐨勬搷浣滆€屼笉鏄? DataType::add銆傚湪杩涜杩愮畻鍓嶏紝搴旇璁剧疆濂界粨鏋滅殑绫诲瀷锛?
 * 姣斿杩涜涓や釜INT绫诲瀷鐨勯櫎娉曡繍绠楁椂锛岀粨鏋滅被鍨嬪簲璇ヨ缃负FLOAT銆?
 */
class Value final
{
public:
  friend class DataType;
  friend class IntegerType;
  friend class FloatType;
  friend class BooleanType;
  friend class CharType;
  friend class DateType;
  friend class VectorType;

  Value() = default;

  ~Value() { reset(); }

  Value(AttrType attr_type, char *data, int length = 4) : attr_type_(attr_type) { this->set_data(data, length); }

  explicit Value(int val);
  explicit Value(float val);
  explicit Value(bool val);
  explicit Value(const char *s, int len = 0);
  explicit Value(const string_t &val);

  Value(const Value &other);
  Value(Value &&other);

  Value &operator=(const Value &other);
  Value &operator=(Value &&other);

  void reset();

  static RC add(const Value &left, const Value &right, Value &result)
  {
    return DataType::type_instance(result.attr_type())->add(left, right, result);
  }

  static RC subtract(const Value &left, const Value &right, Value &result)
  {
    return DataType::type_instance(result.attr_type())->subtract(left, right, result);
  }

  static RC multiply(const Value &left, const Value &right, Value &result)
  {
    return DataType::type_instance(result.attr_type())->multiply(left, right, result);
  }

  static RC divide(const Value &left, const Value &right, Value &result)
  {
    return DataType::type_instance(result.attr_type())->divide(left, right, result);
  }

  static RC negative(const Value &value, Value &result)
  {
    return DataType::type_instance(result.attr_type())->negative(value, result);
  }

  static RC cast_to(const Value &value, AttrType to_type, Value &result)
  {
    if (value.is_null()) {
      result.set_null(to_type);
      return RC::SUCCESS;
    }
    return DataType::type_instance(value.attr_type())->cast_to(value, to_type, result);
  }

  void set_type(AttrType type) { this->attr_type_ = type; }
  void set_data(char *data, int length);
  void set_data(const char *data, int length) { this->set_data(const_cast<char *>(data), length); }
  void set_value(const Value &value);
  void set_boolean(bool val);
  void set_null(AttrType type = AttrType::UNDEFINED);
  bool is_null() const { return is_null_; }

  string to_string() const;

  int compare(const Value &other) const;

  char *data() const;

  int      length() const { return length_; }
  AttrType attr_type() const { return attr_type_; }

public:
  /**
   * 鑾峰彇瀵瑰簲鐨勫€?
   * 濡傛灉褰撳墠鐨勭被鍨嬩笌鏈熸湜鑾峰彇鐨勭被鍨嬩笉绗︼紝灏变細鎵ц杞崲鎿嶄綔
   */
  int      get_int() const;
  float    get_float() const;
  string   get_string() const;
  string_t get_string_t() const;
  bool     get_boolean() const;

public:
  void set_int(int val);
  void set_float(float val);
  void set_string(const char *s, int len = 0);
  void set_empty_string(int len);
  void set_string_from_other(const Value &other);

private:
  AttrType attr_type_ = AttrType::UNDEFINED;
  int      length_    = 0;
  bool     is_null_   = false;

  union Val
  {
    int32_t int_value_;
    float   float_value_;
    bool    bool_value_;
    char   *pointer_value_;
  } value_ = {.int_value_ = 0};

  /// 鏄惁鐢宠骞跺崰鏈夊唴瀛?, 鐩墠瀵逛簬 CHARS 绫诲瀷 own_data_ 涓簍rue, 鍏朵綑绫诲瀷 own_data_ 涓篺alse
  bool own_data_ = false;
};
