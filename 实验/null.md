# null

## 目标

支持字段可空性、`NULL` 字面量、`IS NULL` / `IS NOT NULL` 判断，以及聚合和输出中的空值语义。

## 实现要点

- 字段定义支持 `NULL` 和 `NOT NULL`，默认 `NOT NULL`。
- `FieldMeta` 增加 `nullable` 元数据，并在表元数据 JSON 中序列化。
- `Value` 增加 `is_null` 状态，不新增独立类型；空值保留目标字段类型。
- 记录格式在系统字段之后、普通字段之前增加 null bitmap，普通字段 offset 后移。
- 插入和更新时，nullable 字段允许写入 `NULL`，非 nullable 字段写入 `NULL` 返回失败。
- `RowTuple::cell_at` 读取字段前先检查 bitmap，字段为空时返回带类型的 null `Value`。
- 普通比较中任意一侧为 `NULL` 时结果为 false；`IS NULL` 和 `IS NOT NULL` 只检查左侧空值状态。
- 聚合函数忽略 `NULL`；`count(col)` 返回非空数量，`sum/avg/min/max` 全为空时返回 `NULL`。
- plain 输出通过 `Value::to_string()` 显示 `NULL`，MySQL 协议输出使用 null marker。

## 建议测试

- nullable 字段插入 `NULL` 成功，not null 字段插入 `NULL` 失败。
- `col is null`、`col is not null`、`null is null`、`col = null` 的语义。
- `count(*)`、`count(col)`、`avg/min/max/sum` 在混合空值和全空值场景下的结果。
- `UPDATE ... SET nullable_col = NULL` 与 not null 字段更新失败。
