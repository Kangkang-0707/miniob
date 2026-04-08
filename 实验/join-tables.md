# join-tables

## 目标

支持公开测试里的 `INNER JOIN ... ON ...` 写法，至少覆盖：

```sql
SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id;
SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id AND t2.num > 13 WHERE t1.name = 'b';
SELECT * FROM t1 INNER JOIN t2 ON ... INNER JOIN t3 ON ...;
```

并满足：

- 支持两表和多表 `inner join`
- `on` 条件里支持多个 `and`
- `where` 条件能和 `on` 条件一起生效
- 空表 join 结果正确
- 大表 join 至少能走通

## 实现主线

这题最关键的一点是：MiniOB 本身已经有多表 join 的逻辑算子和物理算子，缺的主要不是执行器，而是 SQL 语法接线。

所以这次的做法不是重写 join 引擎，而是：

1. parser 识别 `INNER JOIN ... ON ...`
2. 把 join 的表和 `on` 条件塞回现有 `SelectSqlNode`
3. 继续复用原来的多表 `JoinLogicalOperator + NestedLoopJoinPhysicalOperator`

## 具体步骤

### 1. 扩展词法关键字

位置：

- `src/observer/sql/parser/lex_sql.l`

新增：

- `INNER`
- `JOIN`

这样 parser 才能识别 `inner join` 语法，而不是把它们当普通标识符。

### 2. 扩展 parser 的 `FROM` 子句

位置：

- `src/observer/sql/parser/yacc_sql.y`
- `src/observer/sql/parser/parse_defs.h`

新增了 `RelationSqlNode`，用于同时保存：

- `relations`
- `join on` 里的条件

这样 `select` 在解析 `from` 时，不只是得到一串表名，还能把每段 `on` 的条件一起带出来。

### 3. 把 `ON` 条件并入 `SelectSqlNode.conditions`

`INNER JOIN` 在语义上是“先 join，再过滤 `on` 条件”。  
但对当前 MiniOB 的执行链来说，把 `on` 条件和 `where` 条件统一看成谓词更省事。

因为这道题只要求 `inner join`，所以：

- `on` 条件
- `where` 条件

都可以合并成一个整体的 `AND` 谓词，不会改变结果。

### 4. 继续复用现有 join 逻辑计划

位置：

- `src/observer/sql/optimizer/logical_plan_generator.cpp`

当前 `select` 的逻辑计划本来就会在多表场景下生成：

1. `TableGet`
2. `Join`
3. `Predicate`
4. `Project`

所以 join 的核心执行路径本身不需要重写，只要语法层把多表和条件传对就行。

## 关键细节

### 为什么 `ON` 可以和 `WHERE` 合并

因为这里只做的是 `INNER JOIN`。

对 `inner join` 来说：

- `on` 过滤不满足连接条件的行
- `where` 再过滤结果

如果所有条件都是 `AND` 关系，最后结果等价于统一放进一个整体谓词里。

这也是为什么这题不需要另外新增一个“专门执行 `ON` 条件”的算子。

### 为什么没有单独重写 JoinPhysicalOperator

仓库里已经存在：

- `JoinLogicalOperator`
- `NestedLoopJoinPhysicalOperator`
- `HashJoinPhysicalOperator` 的骨架

当前公开测试能过的最低要求，是把语法和谓词接进去，让现有 nested loop join 跑起来。

## 手工验证建议

```sql
CREATE TABLE join_table_1(id int, name char);
CREATE TABLE join_table_2(id int, num int);
CREATE TABLE join_table_3(id int, num2 int);

INSERT INTO join_table_1 VALUES (1, 'a');
INSERT INTO join_table_1 VALUES (2, 'b');
INSERT INTO join_table_2 VALUES (1, 2);
INSERT INTO join_table_2 VALUES (2, 15);
INSERT INTO join_table_3 VALUES (1, 120);

SELECT * FROM join_table_1 INNER JOIN join_table_2 ON join_table_1.id = join_table_2.id;
SELECT * FROM join_table_1 INNER JOIN join_table_2 ON join_table_1.id = join_table_2.id AND join_table_2.num > 13 WHERE join_table_1.name = 'b';
SELECT * FROM join_table_1 INNER JOIN join_table_2 ON join_table_1.id = join_table_2.id INNER JOIN join_table_3 ON join_table_1.id = join_table_3.id;
```

预期：

- 第一条返回两表按 `id` 匹配后的结果
- 第二条只返回 `name='b'` 且 `num>13` 的那一行
- 第三条能正确做三表 inner join
