# simple-sub-query

## 目标

支持公开测试里的简单子查询，重点覆盖两类：

1. `IN / NOT IN`
2. 标量子查询比较

例如：

```sql
SELECT * FROM ssq_1 WHERE id IN (SELECT ssq_2.id FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 = (SELECT AVG(ssq_2.col2) FROM ssq_2);
SELECT * FROM ssq_1 WHERE feat1 >= (SELECT MIN(ssq_2.feat2) FROM ssq_2);
```

## 实现主线

这题如果从“通用子查询执行器”角度做，会很大。  
但 `simple-sub-query` 的公开测试其实有一个很重要的特点：

- 只涉及简单 `select`
- 不涉及相关子查询
- 子查询结果可以在外层查询开始前先算出来

所以这次采用的是：

1. parser 识别子查询语法
2. 在 `SelectStmt::create` 阶段先执行子查询
3. 把子查询结果重写成普通谓词表达式
4. 后面的执行阶段继续复用原有 `Predicate` 流程

## 具体步骤

### 1. 扩展条件节点

位置：

- `src/observer/sql/parser/parse_defs.h`

给 `ConditionSqlNode` 增加了：

- `left_is_sub_query`
- `right_is_sub_query`
- `left_sub_query`
- `right_sub_query`

这样一个条件的左右两边除了“字段/常量”，还可以表示“子查询”。

### 2. 扩展 parser 语法

位置：

- `src/observer/sql/parser/lex_sql.l`
- `src/observer/sql/parser/yacc_sql.y`

新增关键字：

- `IN`
- `NOT`

并补了这些语法：

```sql
rel_attr comp_op (select ...)
(select ...) comp_op rel_attr
rel_attr IN (select ...)
rel_attr NOT IN (select ...)
```

### 3. 在 `SelectStmt::create` 中提前执行子查询

位置：

- `src/observer/sql/stmt/select_stmt.cpp`

这里是这题真正的核心。

实现思路是：

1. 先把子查询递归地构造成 `SelectStmt`
2. 生成逻辑计划和物理计划
3. 直接跑一次子查询
4. 把子查询结果取回成 `Value` 列表

因为 `simple-sub-query` 里的子查询和外层当前行无关，所以可以这样“先算一次，再复用”。

### 4. 把子查询重写成普通谓词

#### 标量子查询

例如：

```sql
col1 = (select avg(col2) from ssq_2)
```

执行完子查询后，如果拿到一个值，比如 `4`，就直接重写成：

```sql
col1 = 4
```

如果子查询返回：

- 0 行：重写成恒假表达式
- 1 行 1 列：正常比较
- 多于 1 行：报错
- 不是 1 列：报错

#### `IN / NOT IN`

例如：

```sql
id IN (select ssq_2.id from ssq_2)
```

如果子查询结果是 `{1,2,5}`，就重写成：

```sql
id = 1 OR id = 2 OR id = 5
```

`NOT IN` 则重写成：

```sql
id <> 1 AND id <> 2 AND id <> 5
```

如果结果集为空：

- `IN (empty)` -> 恒假
- `NOT IN (empty)` -> 恒真

### 5. 让 select 直接保存谓词表达式

位置：

- `src/observer/sql/stmt/select_stmt.h`
- `src/observer/sql/stmt/select_stmt.cpp`
- `src/observer/sql/optimizer/logical_plan_generator.cpp`

原来的 `select` 依赖 `FilterStmt`。  
这次改成让 `SelectStmt` 直接保存一个谓词表达式 `predicate_expr_`，因为：

- `IN` 展开后会出现 `OR`
- 子查询重写更适合表达式树

后面逻辑计划生成时直接创建 `PredicateLogicalOperator` 即可。

### 6. 调整谓词下推规则

位置：

- `src/observer/sql/optimizer/predicate_pushdown_rewriter.cpp`

原来的规则遇到 `OR` 会直接返回 `UNIMPLEMENTED`。  
但 `IN` 被展开后天然会有 `OR`，所以这里改成：

- `OR` 不下推
- 但也不报错

这样谓词仍然留在 `Predicate` 算子里执行，功能上是正确的。

## 关键细节

### 为什么这次不做“通用子查询表达式”

因为 `simple-sub-query` 的测试不要求相关子查询，也不要求在投影列里直接放子查询。

如果硬要做成通用运行时子查询表达式，改动会扩散到：

- tuple 求值
- 表达式绑定
- 执行器上下文
- 子查询缓存

而对这道题来说，收益不高。

### 为什么 `select col = (subquery)` 多行要报错

标量子查询必须是“单值”。  
如果子查询返回多行，就不能拿来和单个字段直接比较，所以要失败。

这也正好对应公开测试里的错误用例。

### 为什么聚合子查询在空结果下会变成恒假

当前 MiniOB 这套实现里，没有完整的 SQL `NULL` 语义。  
空表聚合不会产出一行 `NULL`，而是直接 0 行。

所以这里采取的是最稳妥的处理：

- 标量子查询 0 行 -> 条件判定为 false

这样能和当前测试期望对齐。

## 手工验证建议

```sql
CREATE TABLE ssq_1(id int, col1 int, feat1 float);
CREATE TABLE ssq_2(id int, col2 int, feat2 float);

INSERT INTO ssq_1 VALUES (1, 4, 11.2);
INSERT INTO ssq_1 VALUES (2, 2, 12.0);
INSERT INTO ssq_1 VALUES (3, 3, 13.5);
INSERT INTO ssq_2 VALUES (1, 2, 13.0);
INSERT INTO ssq_2 VALUES (2, 7, 10.5);
INSERT INTO ssq_2 VALUES (5, 3, 12.6);

SELECT * FROM ssq_1 WHERE id IN (SELECT ssq_2.id FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 NOT IN (SELECT ssq_2.col2 FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 = (SELECT AVG(ssq_2.col2) FROM ssq_2);
SELECT * FROM ssq_1 WHERE feat1 >= (SELECT MIN(ssq_2.feat2) FROM ssq_2);

SELECT * FROM ssq_1 WHERE col1 = (SELECT ssq_2.col2 FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 IN (SELECT * FROM ssq_2);
```

预期：

- 前四条合法子查询成功
- 多行标量子查询失败
- 多列表 `IN` 子查询失败
