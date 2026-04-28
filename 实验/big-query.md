# big-query

## 1. 题目目标

big-query 关注查询场景下的性能和稳定性。核心不是改变 SQL 结果，而是让大数据量查询能更快、更稳地执行。

典型场景：

- 大表上带条件查询
- 有索引时优先使用索引扫描
- 多个谓词下推到 table scan / index scan
- 扫描过程中尽量少做无意义工作

## 2. 总体思路

MiniOB 的查询优化链路主要在：

- `LogicalPlanGenerator`
- `Rewriter`
- `PredicatePushdownRewriter`
- `PhysicalPlanGenerator`

实现思路是：

1. 先把 `WHERE` 条件构造成表达式树
2. 通过 rewrite 把可以下推的谓词放到 `TableGetLogicalOperator`
3. 物理计划生成时检查谓词中是否存在可用索引
4. 如果找到字段上的索引，就生成 `IndexScanPhysicalOperator`
5. 其他谓词仍保留为 scan 后过滤，保证结果正确

## 3. 关键模块

- `SelectStmt`：构造 predicate expression
- `PredicatePushdownRewriter`：把谓词推到 table get
- `PhysicalPlanGenerator::create_plan(TableGetLogicalOperator &...)`：选择 index scan 或 table scan
- `IndexScanPhysicalOperator`：使用 B+ 树 scanner 取 RID，再回表读 record
- `TableScanPhysicalOperator`：没有可用索引时全表扫描

## 4. 索引选择策略

当前实现采用保守策略：

- 只识别简单比较表达式
- 主要利用字段和常量之间的等值条件
- 找到第一个可用索引后使用它
- 其他条件继续作为 predicates 过滤

这样做的好处是改动小、正确性风险低；坏处是还不是完整的 cost-based optimizer。

## 5. 为什么还要保留谓词过滤

即使用了索引，也不能直接认为所有条件都满足。例如：

```sql
select * from t where id = 1 and score > 80;
```

如果只用 `id` 的索引拿到候选行，还必须继续判断 `score > 80`。所以 index scan 后仍然带着 predicates 做二次过滤。

## 6. 常见边界

- 没有索引：退化成 table scan
- 条件不是字段和常量比较：不走索引
- 字段有索引但条件类型不适合：不走索引或失败
- 索引只负责缩小候选集，最终结果仍由表达式过滤保证

## 7. 验收问答速记

**问：big-query 主要优化在哪里？**
答：主要是在物理计划生成阶段识别可用索引，把原来的全表扫描替换成 `IndexScanPhysicalOperator`，再保留谓词做最终过滤。

**问：为什么用了索引还要再过滤？**
答：索引可能只覆盖一部分条件，其他条件必须继续判断，否则结果会多。

**问：这是完整优化器吗？**
答：不是。这里是规则式优化，优先保证测例需要的索引查询能力，不做复杂代价估算。
