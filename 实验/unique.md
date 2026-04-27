# unique

## 目标

支持唯一索引和多列唯一索引：

```sql
create unique index idx on t(c1);
create unique index idx2 on t(c1, c2);
```

## 实现要点

- 解析层增加 `UNIQUE` 关键字，`CreateIndexSqlNode` 保存 `unique` 和字段列表。
- `IndexMeta` 保存索引字段列表和唯一性，并兼容旧的单字段 `field_name` 元数据。
- 建索引时按字段列表构造 `FieldMeta` 集合，B+ 树索引持有字段元数据副本。
- 单字段索引沿用原始字段字节作为 key；多字段索引用固定长度复合 key，将字段原始字节编码为十六进制字符串后拼接。
- 唯一索引插入前查询已有 key，若存在不同 RID，返回 `RC::RECORD_DUPLICATE_KEY`。
- 唯一索引字段中任意列为 `NULL` 时，该记录不写入该索引，也不参与唯一冲突判断。
- 更新记录时沿用删除旧索引、写记录、插入新索引的流程；新索引插入失败会回滚记录和索引。

## 建议测试

- 单列唯一索引重复插入失败。
- 多列唯一索引只有组合值重复时失败。
- 更新后造成唯一冲突失败并保持原记录。
- 唯一索引列为 `NULL` 时允许多行存在。
