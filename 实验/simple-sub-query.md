# simple-sub-query

## 1. 题目目标

在 MiniOB 现有查询能力的基础上，实现简单子查询，重点包括：

- `IN (subquery)`
- `NOT IN (subquery)`
- 标量子查询比较
- 聚合子查询，如 `AVG / MIN / MAX`
- 空结果子查询的稳定处理
- 多行标量子查询、多列子查询等错误场景

这道题不要求完整支持所有 SQL 子查询语义，重点是把课程测试覆盖的“简单子查询”做通。

## 2. 总体思路

如果从“完整通用子查询执行器”去做，改动会非常大。  
结合测试范围，这里采用的是更适合课程实验的做法：

1. parser 先识别子查询语法
2. 在 `SelectStmt::create` 阶段提前执行子查询
3. 把子查询结果改写成普通谓词表达式
4. 后续继续复用 MiniOB 原有的谓词执行流程

这个方案的关键前提是：

- 题目里的子查询都不是相关子查询
- 子查询结果可以在外层查询真正执行前先算出来

## 3. 具体实现

### 3.1 扩展条件节点

修改位置：

- `src/observer/sql/parser/parse_defs.h`

在 `ConditionSqlNode` 中增加了对子查询和常量列表的描述能力，例如：

- `left_is_sub_query`
- `right_is_sub_query`
- `left_sub_query`
- `right_sub_query`
- `right_is_value_list`
- `right_values`

这样一个条件不再只支持“字段 vs 常量”或“字段 vs 字段”，还可以表示：

- 字段 vs 子查询
- 子查询 vs 字段
- 字段 `IN` 常量列表
- 字段 `IN` 子查询结果

### 3.2 扩展 SQL 语法

修改位置：

- `src/observer/sql/parser/lex_sql.l`
- `src/observer/sql/parser/yacc_sql.y`

补充了这些语法：

```sql
col IN (SELECT ...)
col NOT IN (SELECT ...)
col IN (1, 2, 3)
col NOT IN (1, 2, 3)
col = (SELECT ...)
(SELECT ...) = col
```

这里后期额外补上的一项很关键：

- `IN (5,42,24,30,17)` 这类常量列表

平台私有测试里有这个场景，所以不能只支持 `IN (subquery)`，还要支持 `IN (value list)`。

### 3.3 在 SelectStmt 阶段提前执行子查询

修改位置：

- `src/observer/sql/stmt/select_stmt.cpp`

实现流程是：

1. 递归把子查询构造成 `SelectStmt`
2. 为子查询生成 logical plan / physical plan
3. 直接执行子查询
4. 把子查询结果提取成 `Value` 列表

因为这些子查询和外层当前行无关，所以可以先执行一次，再把结果回填给外层条件。

### 3.4 把子查询改写成普通谓词

#### 标量子查询

例如：

```sql
SELECT * FROM ssq_1 WHERE col1 = (SELECT AVG(ssq_2.col2) FROM ssq_2);
```

如果子查询算出结果 `4`，那外层就可以直接改写为：

```sql
col1 = 4
```

处理规则：

- 0 行：改写成恒假
- 1 行 1 列：正常比较
- 多于 1 行：失败
- 不是 1 列：失败

#### IN / NOT IN

例如：

```sql
id IN (SELECT ssq_2.id FROM ssq_2)
```

如果子查询结果是 `{1,2,5}`，就改写成：

```sql
id = 1 OR id = 2 OR id = 5
```

`NOT IN` 则改写成：

```sql
id <> 1 AND id <> 2 AND id <> 5
```

如果结果为空：

- `IN (empty)` -> 恒假
- `NOT IN (empty)` -> 恒真

### 3.5 完善聚合器支持

修改位置：

- `src/observer/sql/expr/aggregator.h`
- `src/observer/sql/expr/aggregator.cpp`
- `src/observer/sql/expr/expression.cpp`

这是这题中期最关键的一个 bug 点。

一开始基础 `IN` 子查询已经通了，但：

```sql
SELECT AVG(ssq_2.col2) FROM ssq_2;
SELECT MIN(ssq_2.feat2) FROM ssq_2;
```

结果不正确，导致后续聚合标量子查询也不对。

根因是聚合器原本没有把 `AVG / MIN / MAX / COUNT` 这几类完整补齐。

修完以后，这几类子查询才能稳定工作：

```sql
col1 = (SELECT AVG(...))
feat1 >= (SELECT MIN(...))
feat1 <= (SELECT MAX(...))
```

## 4. 这道题最关键的边界问题

### 4.1 空子查询不能崩

例如：

```sql
SELECT * FROM ssq_1 WHERE feat1 < (SELECT MAX(ssq_2.feat2) FROM ssq_2 WHERE 1=0);
```

正确行为应该是：

- 正常执行
- 返回空结果
- 不能断连接，不能超时

这里后来定位到的根因不是子查询本身，而是：

- 空子查询会生成恒假谓词
- 优化器 `predicate_rewrite.cpp` 对恒假谓词的 rewrite 逻辑有问题
- 它在没有真正改树的情况下仍然不断声明“发生了改写”
- 最终造成超时或连接断开

修复方式：

- 只有真的删掉恒真 predicate 时才设置 `change_made = true`
- 恒假谓词保留，不再重复 rewrite

这样空子查询就稳定了。

### 4.2 常量列表 IN

平台还有：

```sql
SELECT * FROM ssq_1 WHERE id IN (5,42,24,30,17);
```

所以不能只支持：

```sql
id IN (SELECT ...)
```

还要支持：

```sql
id IN (value_list)
```

这部分是后期专门补的 parser 和条件构造逻辑。

### 4.3 多行标量子查询和多列子查询要失败

例如：

```sql
SELECT * FROM ssq_1 WHERE col1 = (SELECT ssq_2.col2 FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 IN (SELECT * FROM ssq_2);
```

这些都应该 `FAILURE`，不能误算。

所以实现里明确做了检查：

- 标量子查询必须是单行单列
- `IN` 子查询必须是一列

## 5. 这道题最终改了哪些核心文件

- `src/observer/sql/parser/parse_defs.h`
- `src/observer/sql/parser/yacc_sql.y`
- `src/observer/sql/stmt/select_stmt.cpp`
- `src/observer/sql/stmt/select_stmt.h`
- `src/observer/sql/optimizer/logical_plan_generator.cpp`
- `src/observer/sql/optimizer/predicate_rewrite.cpp`
- `src/observer/sql/expr/aggregator.h`
- `src/observer/sql/expr/aggregator.cpp`
- `src/observer/sql/expr/expression.cpp`

## 6. 手工验证要点

### 6.1 IN / NOT IN 子查询

```sql
SELECT * FROM ssq_1 WHERE id IN (SELECT ssq_2.id FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 NOT IN (SELECT ssq_2.col2 FROM ssq_2);
```

### 6.2 聚合标量子查询

```sql
SELECT * FROM ssq_1 WHERE col1 = (SELECT AVG(ssq_2.col2) FROM ssq_2);
SELECT * FROM ssq_1 WHERE feat1 >= (SELECT MIN(ssq_2.feat2) FROM ssq_2);
SELECT * FROM ssq_1 WHERE feat1 <= (SELECT MAX(ssq_2.feat2) FROM ssq_2);
```

### 6.3 空子查询

```sql
SELECT * FROM ssq_1 WHERE feat1 < (SELECT MAX(ssq_2.feat2) FROM ssq_2 WHERE 1=0);
SELECT * FROM ssq_1 WHERE id IN (SELECT ssq_2.id FROM ssq_2 WHERE 1=0);
SELECT * FROM ssq_1 WHERE id NOT IN (SELECT ssq_2.id FROM ssq_2 WHERE 1=0);
```

验证点：

- 正常执行
- 不超时
- 结果符合空集语义

### 6.4 错误场景

```sql
SELECT * FROM ssq_1 WHERE col1 = (SELECT ssq_2.col2 FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 = (SELECT * FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 IN (SELECT * FROM ssq_2);
SELECT * FROM ssq_1 WHERE col1 NOT IN (SELECT * FROM ssq_2);
```

验证点：

- 都应该 `FAILURE`

### 6.5 常量列表 IN

```sql
SELECT * FROM ssq_1 WHERE id IN (5,42,24,30,17);
```

验证点：

- 能正确命中所有列表内的值

## 7. 汇报时可以怎么讲

这题你可以分三层讲：

1. 我没有去做完整通用子查询执行器，而是结合实验要求，采用“先执行子查询，再改写成普通谓词”的方案。
2. 基础难点在 parser 和条件表达能力，后续难点在聚合器补全和空子查询稳定性。
3. 真正踩坑最多的是边界条件：空子查询、常量列表 IN、多行标量子查询和多列子查询。

如果老师追问“最难的 bug 是什么”，你可以直接讲：

- 一开始 `WHERE feat1 < (SELECT MAX(...) WHERE 1=0)` 会把连接打断
- 最后定位到不是子查询本身，而是优化器对恒假 predicate 的 rewrite 逻辑有问题
- 修完后，空子查询才真正稳定
