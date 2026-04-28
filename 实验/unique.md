# unique

## 1. 题目目标

unique 题目要求支持唯一索引，包括：

- 单列唯一索引
- 多列唯一索引
- 插入重复 key 失败
- 更新后造成重复 key 失败
- 失败时记录和索引都要回滚
- NULL 列不参与唯一冲突检查

示例：

```sql
create unique index idx_id on t(id);
create unique index idx_ab on t(a, b);
```

## 2. 总体思路

唯一索引本质上仍然使用 B+ 树索引，只是在插入索引项前额外检查同 key 是否已经存在。

实现分为四层：

1. parser 支持 `CREATE UNIQUE INDEX`
2. `IndexMeta` 记录 `unique` 和字段列表
3. B+ 树索引支持单列/多列 key 构造
4. insert/update 维护索引时做唯一冲突检查和回滚

## 3. 关键模块

- `CreateIndexSqlNode`
- `CreateIndexStmt`
- `IndexMeta`
- `TableMeta`
- `HeapTableEngine::create_index`
- `BplusTreeIndex::insert_entry`
- `HeapTableEngine::insert_record`
- `HeapTableEngine::update_record_with_trx`

## 4. SQL 和元数据

parser 把：

```sql
create unique index idx on t(a, b);
```

解析为：

- `index_name = idx`
- `relation_name = t`
- `attribute_names = [a, b]`
- `unique = true`

`IndexMeta` 中保存：

- 索引名
- 字段列表
- 是否 unique

同时兼容旧的单字段 `field_name` 元数据。

## 5. 复合 key 构造

单字段索引沿用原来的字段字节作为 key。

多字段索引采用固定长度复合 key：

1. 按索引字段顺序取出每个字段的原始存储字节
2. 把每个字节编码成两位十六进制字符
3. 拼接成一个固定长度字符串 key

这样可以避免直接拼二进制时出现 `\0` 导致字符串比较提前截断。

## 6. 唯一冲突检查

插入索引项时：

1. 先根据 record 构造 key
2. 如果索引是 unique，用 B+ 树查询同 key 的 RID
3. 如果存在不同 RID，返回 `RC::RECORD_DUPLICATE_KEY`
4. 否则正常插入

同一个 RID 的情况不会被判冲突，这对更新回滚和重复维护很重要。

## 7. 更新回滚

更新记录时不能只改 record，还要同步索引：

1. 删除旧记录的索引项
2. 写入新 record
3. 插入新 record 的索引项
4. 如果新索引项插入失败：
   - 删除已经插入的新索引项
   - 恢复旧 record
   - 重新插入旧索引项

这保证了唯一冲突时，表数据和索引数据不会只改一半。

## 8. NULL 与唯一索引

如果唯一索引的任意组成列为 NULL：

- 不写入该索引项
- 不参与唯一冲突判断

原因是 NULL 不应该被当成普通相等值，否则多行 `NULL` 会互相冲突，和平台测例期望不一致。

## 9. 关键边界

- 对已有重复数据创建 unique index：扫描旧数据时会检测到重复并失败
- 更新造成重复：失败并回滚
- 多列唯一索引只在组合 key 相同才冲突
- NULL 组合不冲突
- 多列 update 必须先把所有字段写到同一个 new record，再统一检查索引

## 10. 验收问答速记

**问：唯一索引在哪里检查重复？**
答：在 `BplusTreeIndex::insert_entry` 中。普通索引直接插入，unique 索引会先用 key 查已有 RID，如果已有不同 RID 就返回重复 key。

**问：为什么更新时容易出错？**
答：更新不只是覆盖 record，还要删除旧索引、写新 record、插入新索引。任何一步失败都要回滚，否则索引和表会不一致。

**问：多列唯一索引怎么比较？**
答：把多个字段按定义顺序编码成一个固定长度复合 key，B+ 树仍然只比较一个 key。

**问：NULL 在唯一索引中怎么处理？**
答：只要任一索引列是 NULL，就跳过该索引项，不检查冲突，也不写入索引。
