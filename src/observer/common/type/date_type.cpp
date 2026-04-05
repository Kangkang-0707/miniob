#include "common/type/date_type.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "common/value.h"

DateType DateType::instance_;

const DateType &DateType::instance()
{
  return instance_;
}

static bool is_leap_year(int year)
{
  return (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
}

static int days_in_month(int year, int month)
{
  static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    return is_leap_year(year) ? 29 : 28;
  }
  return days[month];
}

static bool is_all_digits(const string &s)
{
  if (s.empty()) {
    return false;
  }

  for (char ch : s) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

static RC parse_date_string(const char *str, int32_t &date_value)
{
  if (str == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  string s = str;
  if (s.length() >= 2 && s.front() == '\'' && s.back() == '\'') {
    s = s.substr(1, s.length() - 2);
  }

  size_t first_dash = s.find('-');
  if (first_dash == string::npos) {
    return RC::INVALID_ARGUMENT;
  }

  size_t second_dash = s.find('-', first_dash + 1);
  if (second_dash == string::npos) {
    return RC::INVALID_ARGUMENT;
  }

  if (s.find('-', second_dash + 1) != string::npos) {
    return RC::INVALID_ARGUMENT;
  }

  string year_str  = s.substr(0, first_dash);
  string month_str = s.substr(first_dash + 1, second_dash - first_dash - 1);
  string day_str   = s.substr(second_dash + 1);

  if (year_str.length() != 4 || month_str.empty() || month_str.length() > 2 || day_str.empty() || day_str.length() > 2) {
    return RC::INVALID_ARGUMENT;
  }

  if (!is_all_digits(year_str) || !is_all_digits(month_str) || !is_all_digits(day_str)) {
    return RC::INVALID_ARGUMENT;
  }

  int year  = std::atoi(year_str.c_str());
  int month = std::atoi(month_str.c_str());
  int day   = std::atoi(day_str.c_str());

  if (month < 1 || month > 12) {
    return RC::INVALID_ARGUMENT;
  }

  int max_day = days_in_month(year, month);
  if (day < 1 || day > max_day) {
    return RC::INVALID_ARGUMENT;
  }

  date_value = year * 10000 + month * 100 + day;
  return RC::SUCCESS;
}

static void set_date_value(Value &val, int32_t date_value)
{
  val.reset();
  val.set_type(AttrType::DATES);
  val.set_data(reinterpret_cast<char *>(&date_value), sizeof(date_value));
}

int DateType::compare(const Value &left, const Value &right) const
{
  int32_t lv = left.get_int();
  int32_t rv = right.get_int();

  if (lv < rv) {
    return -1;
  }
  if (lv > rv) {
    return 1;
  }
  return 0;
}

RC DateType::cast_to(const Value &val, AttrType type, Value &result) const
{
  if (type == AttrType::DATES) {
    if (val.attr_type() == AttrType::DATES) {
      result = val;
      return RC::SUCCESS;
    }

    if (val.attr_type() == AttrType::CHARS) {
      int32_t date_value = 0;
      RC      rc         = parse_date_string(val.get_string().c_str(), date_value);
      if (rc != RC::SUCCESS) {
        return rc;
      }
      set_date_value(result, date_value);
      return RC::SUCCESS;
    }

    return RC::INVALID_ARGUMENT;
  }

  if (type == AttrType::CHARS && val.attr_type() == AttrType::DATES) {
    string s;
    RC     rc = to_string(val, s);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    result.set_string(s.c_str());
    return RC::SUCCESS;
  }

  return RC::UNSUPPORTED;
}

RC DateType::set_value_from_str(Value &val, const string &data) const
{
  int32_t date_value = 0;
  RC      rc         = parse_date_string(data.c_str(), date_value);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  set_date_value(val, date_value);
  return RC::SUCCESS;
}

RC DateType::to_string(const Value &val, string &result) const
{
  int32_t v = val.get_int();

  int year  = v / 10000;
  int month = (v / 100) % 100;
  int day   = v % 100;

  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
  result = buf;
  return RC::SUCCESS;
}

int DateType::cast_cost(AttrType type)
{
  if (type == AttrType::DATES) {
    return 0;
  }
  if (type == AttrType::CHARS) {
    return 2;
  }
  return INT32_MAX;
}
