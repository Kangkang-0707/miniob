# multi-index

## 1. 题目目标

multi-index 要支持多个字段组成一个索引，例如：

```sql
create index idx_ab on t(a, b);
```

这和“一个表上有多个单列索引”不同。多列索引是一个索引对象，key 由多个字段按顺序拼接组成。

## 2. 总体思路

实现多列索引需要改三层：

1. parser：`CREATE INDEX` 的字段从单个字段扩展为字段列表
2. metadata：`IndexMeta` 从保存一个 `field_name` 扩展为保存 `field_names`
3. storage index：B+ 树 key 从单字段字节扩展为复合 key

为了兼容旧元数据，`IndexMeta` 仍保留单字段访问 helper，旧的 `field_name` JSON 也能反序列化。

## 3. 关键模块

- `CreateIndexSqlNode`
- `CreateIndexStmt`
- `IndexMeta`
- `TableMeta::find_index_by_field`
- `HeapTableEngine::create_index`
- `BplusTreeIndex::make_key`

## 4. 复合 key 设计

单字段索引沿用原来的 key 编码，直接复制字段存储字节。

多字段索引按字段顺序拼接每列数据。为了避免 `char` 比较和二进制 `\0` 截断问题，复合 key 使用固定长度十六进制字符串：

- 原始字节 `0x12` 编成 `"12"`
- 原始字节 `0xAB` 编成 `"AB"`
- 多个字段按顺序连续拼接

这样 B+ 树仍能把 key 当作 `CHARS` 比较，同时避免中间出现 `\0`。

## 5. 元数据兼容

`IndexMeta` 写出时保存：

- `name`
- `field_names`
- `unique`

如果只有一个字段，也可以保留旧的 `field_name`。读取旧表时，如果没有 `field_names`，就退回读取 `field_name`。

## 6. 索引扫描限制

当前查询优化里，`find_index_by_field` 仍然只把单字段索引用作普通谓词加速。多列索引主要用于建索引、维护索引和唯一索引约束，不贸然扩展到复杂前缀扫描。

这样做的原因是：

- 多列索引扫描要处理前缀匹配、范围边界、字段顺序
- 当前测例更关注建索引和唯一约束
- 保守实现能降低错误结果风险

## 7. 验收问答速记

**问：多列索引和多个单列索引有什么区别？**
答：多列索引只有一个 B+ 树，key 是多个字段按顺序组成的复合 key；多个单列索引则是多个独立 B+ 树。

**问：为什么复合 key 要转十六进制？**
答：因为 B+ 树的 `CHARS` 比较路径对字符串结束符敏感，直接拼二进制可能包含 `\0`，会导致比较或扫描出错。转十六进制后 key 是固定长度可比较字符串。

**问：多列索引能不能直接用于所有查询？**
答：当前实现比较保守，普通索引扫描主要使用单字段索引；多列索引用于多字段索引元数据、索引维护和唯一约束。
