# update

## 目标

支持：

```sql
UPDATE table_name SET col = value WHERE ...;
```

并满足这些行为：

- 更新单行和多行
- 支持无条件更新
- 支持 `where` 多条件过滤
- 支持更新带索引的列
- 表不存在、列不存在、条件列不存在时失败
- 类型不匹配时失败

## 实现主线

`update` 不是 DDL，而是 DML，所以要走完整的执行链路：

`stmt -> logical plan -> physical plan -> trx -> table engine`

这题真正难的地方，不是 parser，而是把“更新一条记录”接进现有存储和索引体系。

## 具体步骤

### 1. 实现 `UpdateStmt`

位置：

- `src/observer/sql/stmt/update_stmt.h`
- `src/observer/sql/stmt/update_stmt.cpp`

这里做了 4 件事：

1. 检查目标表是否存在
2. 检查更新列是否存在
3. 把更新值强制转换成目标列类型
4. 用 `FilterStmt` 解析 `where` 条件

这样可以提前挡住很多错误：

- 更新不存在的列
- 条件里写错列名
- 把字符串塞进 `int` 列

### 2. 在 `Stmt::create_stmt` 中接入 `SCF_UPDATE`

位置：

- `src/observer/sql/stmt/stmt.cpp`

让 SQL 能真正生成 `UpdateStmt`。

### 3. 新增 `UpdateLogicalOperator`

位置：

- `src/observer/sql/operator/update_logical_operator.h`
- `src/observer/sql/operator/update_logical_operator.cpp`

作用：

- 保存目标表
- 保存目标字段
- 保存更新值

逻辑计划结构和 `delete` 很像：

1. 先 `TableGet`
2. 再 `Predicate`
3. 最后 `Update`

### 4. 在 `LogicalPlanGenerator` 中生成 `update` 计划

位置：

- `src/observer/sql/optimizer/logical_plan_generator.h`
- `src/observer/sql/optimizer/logical_plan_generator.cpp`

这里复用了 `delete` 的模式：

- `TableGetLogicalOperator(table, READ_WRITE)`
- 条件存在时挂 `PredicateLogicalOperator`
- 最上层挂 `UpdateLogicalOperator`

### 5. 新增 `UpdatePhysicalOperator`

位置：

- `src/observer/sql/operator/update_physical_operator.h`
- `src/observer/sql/operator/update_physical_operator.cpp`

作用：

1. 先从子节点把所有满足条件的记录取出来
2. 拷贝成独立 `Record`
3. 在副本上修改目标字段
4. 调用事务层 `trx->update_record(...)`

这里特意先把记录复制出来，而不是直接拿扫描器当前页里的内存做更新，避免 child 关闭后记录指针失效。

### 6. 在 `PhysicalPlanGenerator` 中接入

位置：

- `src/observer/sql/optimizer/physical_plan_generator.h`
- `src/observer/sql/optimizer/physical_plan_generator.cpp`

补充了：

- `LogicalOperatorType::UPDATE`
- `create_plan(UpdateLogicalOperator &, ...)`

### 7. 扩展算子类型枚举

位置：

- `src/observer/sql/operator/logical_operator.h/.cpp`
- `src/observer/sql/operator/physical_operator.h/.cpp`

补充：

- `LogicalOperatorType::UPDATE`
- `PhysicalOperatorType::UPDATE`

并把 `update` 标记为“不能走向量化执行”的类型，和 `insert/delete` 保持一致。

### 8. 实现事务层 `update_record`

位置：

- `src/observer/storage/trx/vacuous_trx.h/.cpp`
- `src/observer/storage/trx/mvcc_trx.h/.cpp`

目前做法是统一委托给：

```cpp
table->update_record_with_trx(old_record, new_record, this)
```

这样事务层只负责调度，真正的更新逻辑交给表引擎。

### 9. 实现 `HeapTableEngine::update_record_with_trx`

位置：

- `src/observer/storage/table/heap_table_engine.h`
- `src/observer/storage/table/heap_table_engine.cpp`

这是整题最核心的部分。

更新一条记录时，不能只改数据页，还要同时维护索引，所以流程是：

1. 删除旧索引项
2. 原地更新记录内容
3. 插入新索引项

如果任一步失败，要回滚：

- 更新记录失败：把旧索引项补回去
- 新索引插入失败：撤掉已插入的新索引项，恢复旧记录，再把旧索引补回去

这样能保证索引和数据的一致性。

## 关键细节

### 为什么不能直接改扫描器里的当前记录

因为扫描器返回的记录很多时候只是指向页内存的一个视图，child 关闭后这块内存引用就不稳定了。先复制一份 `Record` 更安全。

### 为什么更新要按“删旧索引 -> 改记录 -> 加新索引”

因为索引是按字段值建的。字段值一旦变了，旧键和新键都要处理。

如果只改记录不改索引，就会出现：

- 数据已经变了
- 索引还指向旧值

之后查索引就会错。

### 为什么字符串列要先清空再写入

`char` 字段是定长的。如果新字符串更短，不先清零就会把旧尾巴残留下来。

### 为什么要在 `UpdateStmt` 阶段做类型转换

这样错误可以尽早暴露，不用等走到存储层才报错，也更符合“语义检查”的职责划分。

## 手工验证建议

```sql
CREATE TABLE update_table(id int, num int, name char(10));
INSERT INTO update_table VALUES (1, 10, 'a');
INSERT INTO update_table VALUES (2, 20, 'b');
CREATE INDEX idx_num ON update_table(num);

UPDATE update_table SET num = 99 WHERE id = 1;
SELECT * FROM update_table;

UPDATE update_table SET name = 'hello' WHERE num = 20;
SELECT * FROM update_table;

UPDATE update_table SET num = 100;
SELECT * FROM update_table;

UPDATE not_exist SET num = 1;
UPDATE update_table SET not_exist = 1;
UPDATE update_table SET num = 'abc';
```

预期：

- 合法更新成功
- 带索引列更新成功
- 无条件更新成功
- 不存在表/列时报错
- 类型不匹配时报错
