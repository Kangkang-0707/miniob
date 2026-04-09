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
// Created by Wangyunlai on 2022/6/6.
//

#include "sql/stmt/select_stmt.h"

#include "common/lang/string.h"
#include "common/log/log.h"
#include "session/session.h"
#include "sql/expr/expression.h"
#include "sql/expr/tuple.h"
#include "sql/operator/logical_operator.h"
#include "sql/operator/physical_operator.h"
#include "sql/optimizer/logical_plan_generator.h"
#include "sql/optimizer/physical_plan_generator.h"
#include "sql/optimizer/rewriter.h"
#include "sql/parser/expression_binder.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

using namespace std;
using namespace common;

namespace {

int implicit_cast_cost(AttrType from, AttrType to)
{
  if (from == to) {
    return 0;
  }
  return DataType::type_instance(from)->cast_cost(to);
}

bool is_numeric_type(AttrType type)
{
  return type == AttrType::INTS || type == AttrType::FLOATS;
}

unique_ptr<Expression> make_bool_expression(bool value)
{
  return make_unique<ValueExpr>(Value(value));
}

RC normalize_comparison_expression(unique_ptr<Expression> &expr);

RC normalize_predicate_expression(unique_ptr<Expression> &expr)
{
  if (expr == nullptr) {
    return RC::SUCCESS;
  }

  switch (expr->type()) {
    case ExprType::COMPARISON: return normalize_comparison_expression(expr);
    case ExprType::CONJUNCTION: {
      auto *conjunction_expr = static_cast<ConjunctionExpr *>(expr.get());
      for (auto &child : conjunction_expr->children()) {
        RC rc = normalize_predicate_expression(child);
        if (OB_FAIL(rc)) {
          return rc;
        }
      }
    } break;
    default: break;
  }
  return RC::SUCCESS;
}

RC normalize_comparison_expression(unique_ptr<Expression> &expr)
{
  auto *comparison_expr = static_cast<ComparisonExpr *>(expr.get());
  RC    rc              = normalize_predicate_expression(comparison_expr->left());
  if (OB_FAIL(rc)) {
    return rc;
  }

  rc = normalize_predicate_expression(comparison_expr->right());
  if (OB_FAIL(rc)) {
    return rc;
  }

  unique_ptr<Expression> &left  = comparison_expr->left();
  unique_ptr<Expression> &right = comparison_expr->right();
  if (left->value_type() == right->value_type()) {
    return RC::SUCCESS;
  }

  if (left->type() == ExprType::VALUE && right->type() == ExprType::VALUE) {
    auto *left_value_expr  = static_cast<ValueExpr *>(left.get());
    auto *right_value_expr = static_cast<ValueExpr *>(right.get());

    const Value &left_value  = left_value_expr->get_value();
    const Value &right_value = right_value_expr->get_value();

    auto try_normalize_char_numeric = [&](const Value &char_value, AttrType target_type, bool normalize_left) -> bool {
      if (char_value.attr_type() != AttrType::CHARS || !is_numeric_type(target_type)) {
        return false;
      }

      Value normalized;
      RC    cast_rc = DataType::type_instance(target_type)->set_value_from_str(normalized, char_value.get_string());
      if (OB_FAIL(cast_rc)) {
        expr = make_bool_expression(false);
      } else if (normalize_left) {
        left = make_unique<ValueExpr>(normalized);
      } else {
        right = make_unique<ValueExpr>(normalized);
      }
      return true;
    };

    if (try_normalize_char_numeric(left_value, right_value.attr_type(), true) ||
        try_normalize_char_numeric(right_value, left_value.attr_type(), false)) {
      return RC::SUCCESS;
    }
  }

  auto left_to_right_cost = implicit_cast_cost(left->value_type(), right->value_type());
  auto right_to_left_cost = implicit_cast_cost(right->value_type(), left->value_type());
  if (left_to_right_cost <= right_to_left_cost && left_to_right_cost != INT32_MAX) {
    ExprType left_type = left->type();
    auto     cast_expr = make_unique<CastExpr>(std::move(left), right->value_type());
    if (left_type == ExprType::VALUE) {
      Value left_val;
      rc = cast_expr->try_get_value(left_val);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to cast left constant");
        return rc;
      }
      left = make_unique<ValueExpr>(left_val);
    } else {
      left = std::move(cast_expr);
    }
  } else if (right_to_left_cost < left_to_right_cost && right_to_left_cost != INT32_MAX) {
    ExprType right_type = right->type();
    auto     cast_expr  = make_unique<CastExpr>(std::move(right), left->value_type());
    if (right_type == ExprType::VALUE) {
      Value right_val;
      rc = cast_expr->try_get_value(right_val);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to cast right constant");
        return rc;
      }
      right = make_unique<ValueExpr>(right_val);
    } else {
      right = std::move(cast_expr);
    }
  } else {
    LOG_WARN("unsupported cast from %s to %s",
        attr_type_to_string(left->value_type()),
        attr_type_to_string(right->value_type()));
    return RC::UNSUPPORTED;
  }

  return RC::SUCCESS;
}

RC execute_sub_query(Db *db, shared_ptr<ParsedSqlNode> sub_query_sql, vector<ValueListTuple> &rows)
{
  rows.clear();
  if (sub_query_sql == nullptr || sub_query_sql->flag != SCF_SELECT) {
    return RC::INVALID_ARGUMENT;
  }

  Stmt *sub_stmt_raw = nullptr;
  RC    rc           = SelectStmt::create(db, sub_query_sql->selection, sub_stmt_raw);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create select stmt for sub query. rc=%s", strrc(rc));
    return rc;
  }

  unique_ptr<Stmt> sub_stmt(sub_stmt_raw);
  auto            *select_stmt = static_cast<SelectStmt *>(sub_stmt.get());

  LogicalPlanGenerator logical_plan_generator;
  unique_ptr<LogicalOperator> logical_operator;
  rc = logical_plan_generator.create(select_stmt, logical_operator);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create logical plan for sub query. rc=%s", strrc(rc));
    return rc;
  }

  Rewriter rewriter;
  bool     change_made = false;
  do {
    change_made = false;
    rc          = rewriter.rewrite(logical_operator, change_made);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to rewrite logical plan for sub query. rc=%s", strrc(rc));
      return rc;
    }
  } while (change_made);

  Session session;
  session.set_current_db(db->name());

  Session *old_session = Session::current_session();
  Session::set_current_session(&session);

  PhysicalPlanGenerator physical_plan_generator;
  unique_ptr<PhysicalOperator> physical_operator;
  rc = physical_plan_generator.create(*logical_operator, physical_operator, &session);
  if (OB_FAIL(rc)) {
    session.destroy_trx();
    Session::set_current_session(old_session);
    LOG_WARN("failed to create physical plan for sub query. rc=%s", strrc(rc));
    return rc;
  }

  rc = physical_operator->open(session.current_trx());
  if (OB_FAIL(rc)) {
    session.destroy_trx();
    Session::set_current_session(old_session);
    LOG_WARN("failed to open physical plan for sub query. rc=%s", strrc(rc));
    return rc;
  }

  while (OB_SUCC(rc = physical_operator->next())) {
    Tuple *tuple = physical_operator->current_tuple();
    if (tuple == nullptr) {
      rc = RC::INTERNAL;
      break;
    }

    ValueListTuple row;
    rc = ValueListTuple::make(*tuple, row);
    if (OB_FAIL(rc)) {
      break;
    }
    rows.emplace_back(std::move(row));
  }

  physical_operator->close();
  session.destroy_trx();
  Session::set_current_session(old_session);

  if (rc == RC::RECORD_EOF) {
    return RC::SUCCESS;
  }
  return rc;
}

RC extract_single_column_rows(
    Db *db, shared_ptr<ParsedSqlNode> sub_query_sql, vector<Value> &values, bool expect_single_row)
{
  values.clear();

  vector<ValueListTuple> rows;
  RC                     rc = execute_sub_query(db, sub_query_sql, rows);
  if (OB_FAIL(rc)) {
    return rc;
  }

  if (expect_single_row && rows.size() > 1) {
    LOG_WARN("scalar sub query returned more than one row");
    return RC::INVALID_ARGUMENT;
  }

  for (ValueListTuple &row : rows) {
    if (row.cell_num() != 1) {
      LOG_WARN("sub query should return exactly one column, but got %d", row.cell_num());
      return RC::INVALID_ARGUMENT;
    }

    Value value;
    rc = row.cell_at(0, value);
    if (OB_FAIL(rc)) {
      return rc;
    }
    values.emplace_back(value);
  }

  return RC::SUCCESS;
}

unique_ptr<Expression> make_operand_expression(const ConditionSqlNode &condition, bool left)
{
  if (left) {
    if (condition.left_is_attr) {
      return make_unique<UnboundFieldExpr>(condition.left_attr.relation_name, condition.left_attr.attribute_name);
    }
    return make_unique<ValueExpr>(condition.left_value);
  }

  if (condition.right_is_attr) {
    return make_unique<UnboundFieldExpr>(condition.right_attr.relation_name, condition.right_attr.attribute_name);
  }
  return make_unique<ValueExpr>(condition.right_value);
}

RC build_condition_expression(Db *db, const ConditionSqlNode &condition, unique_ptr<Expression> &expr)
{
  expr = nullptr;

  if (condition.comp == IN_OP || condition.comp == NOT_IN_OP) {
    vector<Value> in_values;
    RC            rc = RC::SUCCESS;
    if (condition.right_is_value_list) {
      in_values = condition.right_values;
    } else {
      rc = extract_single_column_rows(db, condition.right_sub_query, in_values, false);
      if (OB_FAIL(rc)) {
        return rc;
      }
    }

    if (in_values.empty()) {
      expr = make_bool_expression(condition.comp == NOT_IN_OP);
      return RC::SUCCESS;
    }

    auto left_expr = make_operand_expression(condition, true);
    vector<unique_ptr<Expression>> children;
    children.reserve(in_values.size());
    for (const Value &value : in_values) {
      auto right_expr = make_unique<ValueExpr>(value);
      auto comp_expr = make_unique<ComparisonExpr>(
          condition.comp == IN_OP ? EQUAL_TO : NOT_EQUAL, left_expr->copy(), std::move(right_expr));
      children.emplace_back(std::move(comp_expr));
    }

    expr = make_unique<ConjunctionExpr>(
        condition.comp == IN_OP ? ConjunctionExpr::Type::OR : ConjunctionExpr::Type::AND, children);
    return RC::SUCCESS;
  }

  if (condition.left_is_sub_query || condition.right_is_sub_query) {
    vector<Value> left_values;
    vector<Value> right_values;

    RC rc = RC::SUCCESS;
    if (condition.left_is_sub_query) {
      rc = extract_single_column_rows(db, condition.left_sub_query, left_values, true);
      if (OB_FAIL(rc)) {
        return rc;
      }
      if (left_values.empty()) {
        expr = make_bool_expression(false);
        return RC::SUCCESS;
      }
    }

    if (condition.right_is_sub_query) {
      rc = extract_single_column_rows(db, condition.right_sub_query, right_values, true);
      if (OB_FAIL(rc)) {
        return rc;
      }
      if (right_values.empty()) {
        expr = make_bool_expression(false);
        return RC::SUCCESS;
      }
    }

    unique_ptr<Expression> left_expr = condition.left_is_sub_query ? make_unique<ValueExpr>(left_values.front())
                                                                  : make_operand_expression(condition, true);
    unique_ptr<Expression> right_expr = condition.right_is_sub_query ? make_unique<ValueExpr>(right_values.front())
                                                                     : make_operand_expression(condition, false);
    expr = make_unique<ComparisonExpr>(condition.comp, std::move(left_expr), std::move(right_expr));
    return RC::SUCCESS;
  }

  expr = make_unique<ComparisonExpr>(condition.comp, make_operand_expression(condition, true), make_operand_expression(condition, false));
  return RC::SUCCESS;
}

RC build_predicate_expression(
    Db *db, const vector<ConditionSqlNode> &conditions, unique_ptr<Expression> &predicate_expr)
{
  predicate_expr = nullptr;
  if (conditions.empty()) {
    return RC::SUCCESS;
  }

  vector<unique_ptr<Expression>> children;
  children.reserve(conditions.size());
  for (const ConditionSqlNode &condition : conditions) {
    unique_ptr<Expression> condition_expr;
    RC                     rc = build_condition_expression(db, condition, condition_expr);
    if (OB_FAIL(rc)) {
      return rc;
    }
    children.emplace_back(std::move(condition_expr));
  }

  if (children.size() == 1) {
    predicate_expr = std::move(children.front());
  } else {
    predicate_expr = make_unique<ConjunctionExpr>(ConjunctionExpr::Type::AND, children);
  }
  return RC::SUCCESS;
}

}  // namespace

SelectStmt::~SelectStmt() = default;

RC SelectStmt::create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt)
{
  if (nullptr == db) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT;
  }

  BinderContext binder_context;

  vector<Table *> tables;
  for (size_t i = 0; i < select_sql.relations.size(); i++) {
    const char *table_name = select_sql.relations[i].c_str();
    if (nullptr == table_name) {
      LOG_WARN("invalid argument. relation name is null. index=%d", i);
      return RC::INVALID_ARGUMENT;
    }

    Table *table = db->find_table(table_name);
    if (nullptr == table) {
      LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    binder_context.add_table(table);
    tables.push_back(table);
  }

  ExpressionBinder expression_binder(binder_context);

  vector<unique_ptr<Expression>> bound_expressions;
  for (unique_ptr<Expression> &expression : select_sql.expressions) {
    RC rc = expression_binder.bind_expression(expression, bound_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  vector<unique_ptr<Expression>> group_by_expressions;
  for (unique_ptr<Expression> &expression : select_sql.group_by) {
    RC rc = expression_binder.bind_expression(expression, group_by_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  unique_ptr<Expression> predicate_expr;
  RC                     rc = build_predicate_expression(db, select_sql.conditions, predicate_expr);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to build predicate expression. rc=%s", strrc(rc));
    return rc;
  }

  if (predicate_expr != nullptr) {
    vector<unique_ptr<Expression>> bound_predicates;
    rc = expression_binder.bind_expression(predicate_expr, bound_predicates);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to bind predicate expression. rc=%s", strrc(rc));
      return rc;
    }

    if (bound_predicates.size() != 1) {
      LOG_WARN("predicate expression should bind to exactly one expression, but got %d", bound_predicates.size());
      return RC::INVALID_ARGUMENT;
    }

    predicate_expr = std::move(bound_predicates.front());
    rc             = normalize_predicate_expression(predicate_expr);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to normalize predicate expression. rc=%s", strrc(rc));
      return rc;
    }
  }

  SelectStmt *select_stmt = new SelectStmt();
  select_stmt->tables_.swap(tables);
  select_stmt->query_expressions_.swap(bound_expressions);
  select_stmt->predicate_expr_ = std::move(predicate_expr);
  select_stmt->group_by_.swap(group_by_expressions);
  stmt = select_stmt;
  return RC::SUCCESS;
}
