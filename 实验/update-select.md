# update-select

## 1. 题目目标

update-select 要支持 `UPDATE` 语句右侧使用子查询：

```sql
update t set c = (select max(x) from s);
update t set c1 = 1, c2 = (select x from s where id = 1) where id = 10;
```

同时还要支持多列 `SET`：

```sql
update t set a = 1, b = 2 where id = 3;
```

## 2. 总体思路

实现分成两部分：

1. parser 和 AST 支持 `SET` 列表，每个 RHS 可以是常量或子查询
2. update physical operator 在真正更新前先解析 RHS 值，然后批量应用到命中的记录

为了保持简单，当前只支持非相关标量子查询，即子查询不引用外层正在更新的那一行。

## 3. 关键模块

- `UpdateValueSqlNode`
- `UpdateSqlNode::update_values`
- `UpdateStmt`
- `UpdateLogicalOperator`
- `UpdatePhysicalOperator`
- `SelectStmt::create`
- 子查询执行 helper

## 4. 语法设计

`UPDATE` 原来只支持：

```sql
update t set col = value where ...
```

现在扩展为：

```sql
update t set update_value_list where ...
```

每个 `update_value` 可以是：

```sql
col = value
col = (select ...)
```

这样 `SET t_name='A', col2=(select ...)` 可以统一表示为一个列表。

## 5. Stmt 阶段检查

`UpdateStmt::create` 会做：

- 检查目标表存在
- 检查每个更新字段存在
- 常量 RHS 尝试转换到目标字段类型
- `NULL` RHS 检查 nullable
- 子查询 RHS 先保存起来，执行阶段再求值

子查询不在 Stmt 阶段执行，因为执行需要当前 session、事务和物理计划。

## 6. 执行阶段

`UpdatePhysicalOperator::open` 的流程：

1. 打开 child operator，扫描所有满足 WHERE 的目标记录
2. 如果没有命中记录：
   - 常量 RHS 直接成功
   - 子查询 RHS 只做语义合法性检查，不执行行数/赋值校验
3. 如果命中记录：
   - 先执行每个 RHS 子查询，得到最终 `Value`
   - 子查询 0 行返回 NULL
   - 子查询多行返回 FAILURE
   - 子查询多列返回 FAILURE
4. 对每条旧 record 复制出 new record
5. 在 new record 上应用所有 SET 字段
6. 调用 `trx_->update_record` 一次性更新
7. 如果中途失败，反向回滚已经更新的记录

## 7. 子查询语义

支持：

```sql
update t set c = (select max(x) from s);
update t set c = (select x from s where id = 1);
```

规则：

- 必须是 1 列
- 命中更新行时：
  - 0 行：视为 NULL
  - 1 行：取该值
  - 多行：FAILURE
- 外层 WHERE 命中 0 行时：
  - 子查询表/字段不存在：FAILURE
  - 子查询合法但返回多行、0 行或 NULL：都不影响，SUCCESS

最后一条是平台测例中特别容易错的边界，因为没有目标行时不应该做赋值语义校验，但仍应检查 SQL 中引用的表字段是否合法。

## 8. 多列 SET 的关键点

多列更新不能一列一列独立调用 `update_record`。

错误做法：

```text
先改 col1 并写回
再改 col2 并写回
```

这样会导致：

- 中间状态可能违反唯一索引
- 回滚复杂
- 平台测例中 `set id1=1,id2=4` 会只看到第一列变化

正确做法：

```text
复制 old_record -> new_record
把所有 SET 字段都写进 new_record
一次 update_record(old_record, new_record)
```

## 9. 类型转换

常量 RHS 会尽量转换为目标字段类型：

- `int -> char`
- `float -> char`
- `float -> int`，按当前 `Value::get_int()` 截断
- `char -> int/float/date`

这能满足平台中类似：

```sql
update t set name=307, col1=579.04 where id=2;
```

## 10. 验收问答速记

**问：update-select 的子查询什么时候执行？**
答：在物理算子执行阶段，扫描到目标记录后、真正更新前执行。因为这时才有 session、事务和可执行计划。

**问：子查询返回 0 行怎么办？**
答：如果外层命中记录，RHS 视为 NULL；能不能写入取决于目标列是否 nullable。如果外层没命中记录，不做赋值校验，SQL 成功。

**问：外层没命中记录时，为什么不存在表还要失败？**
答：因为 SQL 本身引用了不存在的对象，语义不合法；但合法子查询的结果行数和值不应影响 0 行更新。

**问：多列 SET 为什么要一次性写 new_record？**
答：否则会出现中间状态，尤其会误触发唯一索引冲突，也无法正确表达一条 SQL 对同一行同时更新多个字段。
