# update-select

## 目标

支持 `UPDATE` 的右值为非相关标量子查询：

```sql
update t set c = (select max(x) from s);
```

## 实现要点

- 解析层为 `UpdateSqlNode` 增加子查询右值，常量右值保持原行为。
- `UpdateStmt` 和逻辑、物理算子保存可选的 `ParsedSqlNode` 子查询。
- 执行更新前先执行子查询，并抽取单个 `Value`。
- 子查询必须返回一列；返回多行或多列时失败；返回 0 行时视为 `NULL`。
- 子查询结果会按目标字段类型转换；空结果更新到 not null 字段时失败。
- 物理更新算子先收集目标记录，再统一更新；中途失败时反向回滚已经更新的记录。

## 建议测试

- `update t set c = (select max(x) from s)` 成功。
- 子查询返回 0 行时 nullable 字段变为 `NULL`。
- 子查询返回多行或多列时失败。
- 多行目标记录更新过程中遇到唯一冲突或 not null 失败时能够回滚。
