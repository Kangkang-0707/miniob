# update

## 1. 题目目标

实现 `UPDATE` 功能，支持：

```sql
UPDATE table_name SET col = value WHERE ...;
```

并满足：

- 支持更新单行和多行
- 支持无条件更新
- 支持 `WHERE` 过滤
- 支持更新带索引的列
- 表不存在、列不存在、类型不匹配时返回失败

这道题的核心不是 parser，而是把“更新记录”安全接进 MiniOB 现有的事务、记录和索引体系。

## 2. 总体思路

按照题目思路里的层次，这题需要走完整 DML 链路：

`SQL -> Stmt -> Logical Plan -> Physical Plan -> Trx -> Table Engine`

也就是说，不能只停留在语法识别，还要真正把更新动作落到存储层。

## 3. 具体实现

### 3.1 Resolve 阶段：实现 UpdateStmt

修改位置：

- `src/observer/sql/stmt/update_stmt.h`
- `src/observer/sql/stmt/update_stmt.cpp`

主要负责：

- 检查目标表是否存在
- 检查更新列是否存在
- 检查并解析 `WHERE` 条件
- 把更新值转换成目标列类型

这一层的作用很重要，因为很多错误应该在这里尽早挡住：

- 更新不存在的列
- 条件列写错
- 把字符串赋给 `int`

### 3.2 Stmt 分发：接入 UPDATE

修改位置：

- `src/observer/sql/stmt/stmt.cpp`

把 `SCF_UPDATE` 接进 `Stmt::create_stmt`，让 SQL 能真正生成 `UpdateStmt`。

### 3.3 Logical Plan：新增 UpdateLogicalOperator

修改位置：

- `src/observer/sql/operator/update_logical_operator.h`
- `src/observer/sql/operator/update_logical_operator.cpp`
- `src/observer/sql/optimizer/logical_plan_generator.cpp`

这里做的事情是：

- 保存目标表
- 保存目标字段
- 保存新的值
- 在逻辑计划里形成：

```text
TableGet -> Predicate -> Update
```

如果没有 `WHERE`，就直接是：

```text
TableGet -> Update
```

### 3.4 Physical Plan：新增 UpdatePhysicalOperator

修改位置：

- `src/observer/sql/operator/update_physical_operator.h`
- `src/observer/sql/operator/update_physical_operator.cpp`
- `src/observer/sql/optimizer/physical_plan_generator.cpp`

执行策略是：

1. 从子节点扫描出所有满足条件的记录
2. 复制出独立 `Record`
3. 在副本上修改目标字段
4. 调用事务层执行真正的更新

这里选择“先拷贝再修改”，而不是直接改扫描器当前记录，是为了避免 child 关闭或页切换后指针失效。

### 3.5 事务层：交给 update_record

修改位置：

- `src/observer/storage/trx/vacuous_trx.h`
- `src/observer/storage/trx/vacuous_trx.cpp`
- `src/observer/storage/trx/mvcc_trx.h`
- `src/observer/storage/trx/mvcc_trx.cpp`

这一层主要做调度，把更新委托给表引擎：

```cpp
table->update_record_with_trx(old_record, new_record, this)
```

这样事务层不直接关心索引细节，而是统一交给底层表引擎处理。

### 3.6 存储层：真正修改记录并维护索引

修改位置：

- `src/observer/storage/table/heap_table_engine.h`
- `src/observer/storage/table/heap_table_engine.cpp`

这是这道题最核心的实现。

更新一条记录不能只改数据页，还必须同步维护索引。
最终采用的流程是：

1. 删除旧索引项
2. 更新记录内容
3. 插入新索引项

如果中途失败，就执行回滚，保证：

- 数据和索引一致
- 不会出现“数据改了但索引还是旧的”这种问题

## 4. 这道题的关键难点

### 4.1 难点一：更新不是简单覆盖内存

很多人最开始会把更新理解成：

- 找到记录
- 改字段值

但实际数据库里不够，因为还涉及：

- 索引维护
- 事务接口
- 失败回滚

所以真正难的是“把更新接进整个存储流程”。

### 4.2 难点二：带索引列的更新

如果更新的是带索引字段，例如：

```sql
UPDATE update_table SET num = 99 WHERE id = 1;
```

那就必须同步修改索引，否则后续查索引会错。
这也是为什么存储层要做“删旧索引 -> 改记录 -> 加新索引”的顺序。

### 4.3 难点三：类型检查和同类型赋值

这题后期还踩过一个很典型的坑：

- 合法的 `UPDATE update_table SET num = 99 WHERE id = 1;`
- 居然也失败

原因是更新值处理时把“同类型赋值”也一律丢进了 `cast_to`。
后来修正成：

- 同类型直接赋值
- 不同类型才尝试转换

这样才恢复了合法 `UPDATE` 的正常行为。

## 5. 最终改动的核心文件

- `src/observer/sql/stmt/update_stmt.h`
- `src/observer/sql/stmt/update_stmt.cpp`
- `src/observer/sql/stmt/stmt.cpp`
- `src/observer/sql/operator/update_logical_operator.h`
- `src/observer/sql/operator/update_logical_operator.cpp`
- `src/observer/sql/operator/update_physical_operator.h`
- `src/observer/sql/operator/update_physical_operator.cpp`
- `src/observer/sql/optimizer/logical_plan_generator.cpp`
- `src/observer/sql/optimizer/physical_plan_generator.cpp`
- `src/observer/storage/trx/vacuous_trx.cpp`
- `src/observer/storage/trx/mvcc_trx.cpp`
- `src/observer/storage/table/heap_table_engine.cpp`

## 6. 手工验证要点

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

验证点：

- 合法更新成功
- 带索引字段更新成功
- 无条件更新成功
- 非法表名 / 列名 / 类型不匹配时失败

## 7. 汇报时可以怎么讲

你可以把这题概括成三句：

1. `update` 题真正难的是把更新动作接进事务、记录和索引体系，而不是 parser。
2. 我把它接成了完整的 DML 执行链：`Stmt -> Plan -> Trx -> Table Engine`。
3. 后期关键修复点是带索引字段的更新一致性，以及同类型赋值不应该误走类型转换。
