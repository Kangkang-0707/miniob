# basic

## 1. 题目目标

basic 题目主要验证 MiniOB 最基本的 SQL 执行链路：

- `CREATE TABLE`
- `INSERT`
- `SELECT`
- 简单 `WHERE`
- 基础类型 `int/float/char`
- 普通结果输出

这部分不是某一个孤立功能，而是后续所有题目的地基。

## 2. 总体链路

一条 SQL 在 MiniOB 中大致会经过：

1. `lex_sql.l` / `yacc_sql.y` 解析成 `ParsedSqlNode`
2. `Stmt::create_stmt` 根据语句类型生成具体 `Stmt`
3. `LogicalPlanGenerator` 生成逻辑算子
4. `PhysicalPlanGenerator` 生成物理算子
5. executor 或 physical operator 调用 storage 层
6. communicator 输出结果

basic 的核心是把这条链路跑通，并且保证表元数据、记录数据和 `Value` 对象之间的类型一致。

## 3. 关键模块

- parser：把 SQL 文本变成 AST
- `CreateTableStmt` / `InsertStmt` / `SelectStmt`：做表名、字段名、值数量、类型等检查
- `TableMeta` / `FieldMeta`：保存表结构、字段 offset、字段长度
- `Table::make_record`：把用户输入的 `Value` 写成固定长度 record
- `RowTuple` / `FieldExpr`：从 record 中读取字段值
- `TableScanPhysicalOperator`：逐行扫描表

## 4. 关键设计

### 4.1 表结构和记录格式

建表时，每个字段会被转换成 `FieldMeta`，包含：

- 字段名
- 字段类型
- 字段长度
- 字段在 record 中的 offset

插入时，`Table::make_record` 按照 `FieldMeta` 的 offset 把每个值拷贝到 record 内存中，所以后续读取时只需要根据 offset 和长度反解。

### 4.2 类型流动

SQL 字面量先变成 `Value`，再在写入或比较时根据字段类型做转换。后续的 date、null、update-select 都是在这条 `Value` 流动链路上扩展。

### 4.3 WHERE 过滤

简单条件会被转换成表达式树，例如：

```sql
select * from t where id = 1;
```

会变成一个 `ComparisonExpr(FieldExpr(id), ValueExpr(1))`。扫描时每读一行，就用当前 tuple 计算表达式真假。

## 5. 常见边界

- 插入值数量和字段数量不一致：返回 `FAILURE`
- 表不存在或字段不存在：返回 `FAILURE`
- 字符串长度超过字段长度：按固定长度字段截断或填充
- WHERE 条件没有命中：输出空结果，但 SQL 本身成功

## 6. 验收问答速记

**问：一条 SELECT 是怎么执行的？**
答：先解析成 `ParsedSqlNode`，再生成 `SelectStmt`，之后生成 table scan / predicate / project 等逻辑和物理算子，执行时 `TableScanPhysicalOperator` 逐行取 record，包装成 `RowTuple`，表达式从 tuple 里取字段并计算，最后 communicator 输出。

**问：字段 offset 是哪里决定的？**
答：`TableMeta::init` 根据字段定义顺序计算 offset，`Table::make_record` 和 `RowTuple::cell_at` 都依赖同一份 `FieldMeta`，所以写入和读取能对齐。

**问：为什么后续题目经常改 Value？**
答：因为 parser、表达式、记录读写、聚合、输出都会通过 `Value` 传递数据。扩展新类型或 NULL 时，`Value` 是最核心的数据载体。
