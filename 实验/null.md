# null

## 1. 题目目标

NULL 题目要求 MiniOB 支持 SQL 中的空值语义，包括：

- 建表字段支持 `NULL` / `NOT NULL`
- 支持 `NULL` 字面量
- 支持 `IS NULL` / `IS NOT NULL`
- 普通比较遇到 NULL 返回 false
- 聚合函数忽略 NULL
- 输出层能正确显示 NULL
- `UPDATE` 和 `UPDATE ... SET col=(SELECT...)` 能正确处理 NULL

这题的核心不是简单加一个 `NULL` 类型，而是让空值贯穿解析、记录存储、表达式、聚合和输出。

## 2. 总体思路

最终采用的设计是：

- `Value` 增加 `is_null_` 标记
- 字段类型仍保留原类型，例如 `int null` 的 NULL 仍是 `AttrType::INTS`
- 每条 record 增加 null bitmap，记录每个普通字段是否为空
- 读取字段时先看 bitmap，再决定返回普通值还是 null `Value`

不单独新增 `AttrType::NULLS` 的原因是：NULL 本身不是一种列类型，而是某个类型上的空值状态。这样 `NULL` 写入 `int` 列、`float` 列、`char` 列时都能保留目标列类型。

## 3. 关键模块

- `lex_sql.l` / `yacc_sql.y`：识别 `NULL`、`IS NULL`、`IS NOT NULL`
- `AttrInfoSqlNode`：保存字段 nullable 属性
- `FieldMeta`：保存并序列化 nullable
- `TableMeta`：维护 `null_bitmap_size` 和字段 offset
- `Value`：增加 `is_null_`
- `Table::make_record`：插入时写 bitmap
- `RowTuple::cell_at`：读取时检查 bitmap
- `ComparisonExpr`：实现 NULL 比较语义
- `aggregator.cpp`：聚合忽略 NULL
- communicator：plain/MySQL 输出 NULL

## 4. 记录格式设计

record 中的数据布局变为：

```text
系统字段 | null bitmap | 普通字段数据
```

bitmap 的第 `field_id` 位表示对应普通字段是否为 NULL。

例如有 5 个普通字段，则 bitmap 大小为 1 字节；有 9 个普通字段，则 bitmap 大小为 2 字节。

字段 offset 会统一向后移动 bitmap 大小，保证普通字段数据不会覆盖 bitmap。

## 5. NULL 语义

### 5.1 字段可空性

当前实现中：

- 显式 `NOT NULL`：不允许写 NULL
- 显式 `NULL`：允许写 NULL
- 未显式写时：默认允许 NULL

这个默认值是为了符合平台测例中普通字段可以接收空子查询结果的期望。

### 5.2 普通比较

普通比较中，只要任意一侧是 NULL，结果就是 false：

```sql
where col = null
where col > null
where null <> col
```

这些都不会命中记录。

### 5.3 IS NULL

`IS NULL` 和 `IS NOT NULL` 不做普通比较，而是直接检查 `Value::is_null()`。

```sql
where col is null
where col is not null
```

### 5.4 聚合

聚合函数遵循：

- `count(*)`：统计所有行
- `count(col)`：只统计非 NULL
- `sum/avg/min/max`：忽略 NULL
- 如果输入全是 NULL：
  - `count(col)` 返回 `0`
  - `sum/avg/min/max` 返回 `NULL`

## 6. UPDATE 中的 NULL

普通 update：

```sql
update t set col = null where id = 1;
```

如果 `col` nullable，则设置 bitmap 并清空字段数据；如果 `col not null`，返回 `FAILURE`。

update-select：

```sql
update t set col = (select x from s where id = 100);
```

如果子查询返回 0 行，RHS 被视为 NULL。是否成功取决于：

- 外层 WHERE 命中 0 行：SQL 成功，不做实际赋值校验
- 外层 WHERE 命中记录：目标列 nullable 才能写 NULL

## 7. 关键边界

- NULL 不是字符串 `"NULL"`
- `col = null` 不等价于 `col is null`
- 建表元数据要兼容老表，没有 `nullable/null_bitmap_size` 时按旧格式读取
- MySQL 协议输出要用 null marker，不是输出普通字符串
- unique 索引中，索引列为 NULL 时不参与唯一冲突判断

## 8. 验收问答速记

**问：为什么不用单独的 NULL 类型？**
答：因为 SQL 中 NULL 是某个字段类型上的空值状态，不是独立列类型。`int null`、`float null`、`char null` 都需要保留目标类型，所以在 `Value` 里加 `is_null_` 更合适。

**问：NULL 存在 record 的哪里？**
答：存在每条记录的 null bitmap 中。bitmap 放在系统字段之后、普通字段数据之前，字段数据区仍按原类型固定长度保存。

**问：`col = null` 为什么查不到？**
答：SQL 三值逻辑里普通比较遇到 NULL 不为 true。题目简化成：任意普通比较只要一侧为 NULL，结果就是 false；要判断空值必须用 `IS NULL`。

**问：空子查询更新 NOT NULL 字段为什么有时成功？**
答：如果外层 `WHERE` 没有命中任何行，实际上没有字段被写入，所以 SQL 成功；如果命中了记录，就要按目标字段 nullable 规则检查。
