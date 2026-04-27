# join-tables

## 1. 题目目标

在 MiniOB 已经支持普通多表查询的基础上，补齐 `INNER JOIN ... ON ...` 语法，并支持：

- 两表和多表 `INNER JOIN`
- `ON` 中出现多条 `AND` 条件
- `ON` 条件和 `WHERE` 条件同时存在
- 常量条件参与 `JOIN`
- 空表、空结果场景不超时、不崩溃

这道题的核心不是重写 join 执行器，而是把 `INNER JOIN` 语法正确接到现有的多表查询执行链上。

## 2. 总体思路

MiniOB 原本就有多表查询和 join 执行的基础能力，所以这题采用的是“语法扩展 + 复用原执行链”的方案：

1. 在词法和语法分析阶段识别 `INNER JOIN ... ON ...`
2. 把 `JOIN` 中的表和 `ON` 条件整理回 `SelectSqlNode`
3. 在 `SelectStmt` 阶段把 `ON` 条件和 `WHERE` 条件统一整理成谓词表达式
4. 继续复用原有的 logical plan / physical plan / nested loop join 执行链

这样改动范围相对可控，也更符合课程实验的目标。

## 3. 具体实现

### 3.1 扩展 SQL 语法

修改位置：

- `src/observer/sql/parser/lex_sql.l`
- `src/observer/sql/parser/yacc_sql.y`
- `src/observer/sql/parser/parse_defs.h`

做的事情：

- 增加 `INNER`、`JOIN` 关键字
- 让 `FROM` 子句支持 `table_a INNER JOIN table_b ON ...`
- 支持继续链式解析多张表：
  `t1 INNER JOIN t2 ON ... INNER JOIN t3 ON ...`
- 把 `ON` 条件保存到解析结果中，后续统一进入 `SELECT` 的条件处理逻辑

### 3.2 复用已有多表执行逻辑

修改位置：

- `src/observer/sql/stmt/select_stmt.cpp`
- `src/observer/sql/optimizer/logical_plan_generator.cpp`

实现策略：

- 不新增专门的 join 执行器
- 不改变原有多表扫描和 join 的主链
- 只是在 `SelectStmt` 中把 `ON` 条件和 `WHERE` 条件都转成统一的谓词表达式

因为这里实现的是 `INNER JOIN`，所以：

- `ON` 条件本质上就是过滤条件
- 它和 `WHERE` 中的 `AND` 条件可以统一看待

这也是为什么这题可以不重写执行器。

### 3.3 支持多条 ON 条件

例如：

```sql
SELECT *
FROM join_table_1
INNER JOIN join_table_2
ON join_table_1.id = join_table_2.id
AND join_table_2.num > 13;
```

这里的做法是：

- parser 把多条条件都收集起来
- 后续按 `AND` 关系拼成一个整体谓词树

这样既能支持单条 `ON`，也能支持多条 `ON` 条件。

## 4. 实现过程中遇到的关键问题

### 4.1 问题一：不是 join 算法错，而是语法没接通

一开始最容易误判的是觉得要重写 join 算法，但实际上 MiniOB 原本就有多表执行能力。真正缺的是：

- 不能识别 `INNER JOIN`
- 不能把 `ON` 条件传进后续语义和执行阶段

所以真正关键的工作是 parser 和 `SelectStmt`。

### 4.2 问题二：恒真/恒假条件会引发优化器问题

平台和自测里都出现过这类条件：

```sql
SELECT * FROM join_table_2 INNER JOIN join_table_3 ON 1 > 1.5;
```

正确结果应该是：

- 正常执行
- 返回空结果
- 不能超时，不能崩溃

最后定位到问题在：

- `src/observer/sql/optimizer/predicate_rewrite.cpp`

原因是：

- 某些恒真/恒假谓词在 rewrite 阶段虽然没有真正改动树结构
- 但优化器仍然把 `change_made` 设成了 `true`
- 于是反复重写，造成超时或异常

修复方式：

- 只有在确实删掉恒真 predicate 节点时才把 `change_made` 设为 `true`
- 恒假条件保留，不再假装发生改写

这样：

- `ON 1 > 1.5` 会得到空结果
- 不会再卡住优化器

### 4.3 问题三：类型转换放得太宽会误伤 join 结果

平台私有样例里有这条：

```sql
SELECT * FROM join_table_2 INNER JOIN join_table_3 ON '1.5a' <> 1.5;
```

如果把字符串和数字的隐式转换放得太宽，就会错误地把这个条件算成真，导致：

- `INNER JOIN` 退化成笛卡尔积
- 输出多余结果

这个问题不是 `JOIN` 本身错了，而是“比较表达式归一化”里把类型转换做得过度了。

最终采用的是最小修复策略：

- 在 `src/observer/sql/stmt/select_stmt.cpp` 中
- 只针对“常量 vs 常量”的比较做特殊处理
- 如果是“字符串常量 vs 数值常量”，先尝试按数值解析字符串
- 如果解析失败，就把整个条件折成恒假

这样可以做到：

- `'1.5a' <> 1.5` 不会再错误地产生全表匹配
- 同时不去破坏已经通过的字段比较和其他题目

这是这道题后期最关键的一次收口。

## 5. 这道题最终改了哪些核心文件

- `src/observer/sql/parser/lex_sql.l`
- `src/observer/sql/parser/yacc_sql.y`
- `src/observer/sql/parser/parse_defs.h`
- `src/observer/sql/stmt/select_stmt.cpp`
- `src/observer/sql/stmt/select_stmt.h`
- `src/observer/sql/optimizer/logical_plan_generator.cpp`
- `src/observer/sql/optimizer/predicate_rewrite.cpp`

## 6. 手工验证要点

### 6.1 基础 join

```sql
SELECT * FROM join_table_1 INNER JOIN join_table_2 ON join_table_1.id = join_table_2.id;
```

验证点：

- 两表 join 正常
- 按 `id` 正确匹配

### 6.2 ON + WHERE 联合过滤

```sql
SELECT *
FROM join_table_1
INNER JOIN join_table_2
ON join_table_1.id = join_table_2.id AND join_table_2.num > 13
WHERE join_table_1.name = 'b';
```

验证点：

- `ON` 条件和 `WHERE` 条件同时生效

### 6.3 三表 join

```sql
SELECT *
FROM join_table_1
INNER JOIN join_table_2 ON join_table_1.id = join_table_2.id
INNER JOIN join_table_3 ON join_table_1.id = join_table_3.id;
```

验证点：

- 支持多张表 join

### 6.4 常量条件

```sql
SELECT * FROM join_table_2 INNER JOIN join_table_3 ON 1 > 1.5;
SELECT * FROM join_table_2 INNER JOIN join_table_3 ON '1.5a' <> 1.5;
```

验证点：

- 正常执行
- 空结果
- 不超时，不崩溃

## 7. 汇报时可以怎么讲

可以把这题概括成三句话：

1. `INNER JOIN` 题的核心是语法和条件接线，不是重写 join 执行器。
2. 我复用了 MiniOB 原有的多表 join 执行链，把 `ON` 和 `WHERE` 统一成谓词表达式。
3. 后期重点修了两个边界问题：恒假条件导致优化器超时、非法常量比较导致 join 结果被放大。

如果老师继续追问“难点是什么”，你可以重点讲：

- parser 如何把 `INNER JOIN ... ON ...` 接进来
- 为什么 `ON` 能和 `WHERE` 统一处理
- 为什么要特别处理 `1 > 1.5`、`'1.5a' <> 1.5` 这种常量条件
