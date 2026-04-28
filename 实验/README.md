# MiniOB 实验笔记总览

这个目录记录已经通过的题目实现思路。验收前建议优先看每篇的“验收问答速记”部分，能快速回答老师常问的“改了哪里、为什么这么做、边界怎么处理”。

## 已完成题目

| 题目 | 文档 | 验收重点 |
| --- | --- | --- |
| basic | `basic.md` | SQL 从解析到执行的基础链路，表/记录/值如何流动 |
| big-query | `big-query.md` | 查询性能优化，谓词下推、索引扫描和扫描过滤 |
| big-write | `big-write.md` | 批量写入路径，LOAD DATA / chunk / record page |
| big-order-by | `big-order-by.md` | ORDER BY 语法、字段绑定、阻塞式排序算子 |
| date | `date.md` | 日期内部编码、合法性校验、比较和输出 |
| drop-table | `drop-table.md` | 元数据、数据文件、索引文件的完整删除 |
| update | `update.md` | 更新记录、事务入口、索引删除与回滚 |
| aggregation-func | `aggregation-func.md` | 聚合表达式绑定、非法参数、聚合器执行 |
| like | `like.md` | LIKE/NOT LIKE 语义、模式匹配和类型约束 |
| join-tables | `join-tables.md` | JOIN 语法接入、ON 条件、复用多表执行 |
| simple-sub-query | `simple-sub-query.md` | 非相关子查询执行、标量子查询、IN/NOT IN 改写 |
| multi-index | `multi-index.md` | 多列索引元数据、复合 key、索引扫描限制 |
| null | `null.md` | 可空字段、null bitmap、比较/聚合/update-select 的 NULL 语义 |
| unique | `unique.md` | 唯一索引、多列唯一索引、更新回滚、NULL 与唯一约束 |
| update-select | `update-select.md` | UPDATE RHS 子查询、多列 SET、空命中和失败语义 |

## 验收回答模板

老师问“你这题怎么实现的”时，可以按这四步回答：

1. **SQL 入口**：说明 parser/AST 增加了什么，例如新 token、新语法节点或字段。
2. **语义阶段**：说明 Stmt/Binder 怎么检查表、字段、类型和非法输入。
3. **执行阶段**：说明 LogicalOperator/PhysicalOperator 或存储层怎么真正执行。
4. **边界处理**：说明失败、回滚、NULL、重复 key、空结果等特殊情况。

如果被追问“为什么不直接在 parser 拒绝”，可以回答：MiniOB 很多题目的期望是语法能解析，语义或执行阶段返回 `FAILURE`，否则会变成 `SQL_SYNTAX`，和测例不一致。
