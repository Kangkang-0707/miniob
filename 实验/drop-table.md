# drop-table

## 1. 题目目标

实现：

```sql
DROP TABLE table_name;
```

并满足：

- 可以删除空表和非空表
- 删除后原表不能继续访问
- 可以重新创建同名表
- 删除不存在的表时失败
- 删除带索引的表时要把索引文件一起清理

这道题属于 DDL，重点是把“表对象、元数据文件、数据文件、索引文件”一起清理干净。

## 2. 总体思路

这题不像查询或更新那样要走复杂计划，而是比较直接地走：

`Stmt -> Executor -> Db`

也就是说：

- 先把 SQL 变成 `DropTableStmt`
- 再由 `CommandExecutor` 分发
- 最终落到 `Db::drop_table`

## 3. 具体实现

### 3.1 Resolve 阶段：新增 DropTableStmt

修改位置：

- `src/observer/sql/stmt/drop_table_stmt.h`
- `src/observer/sql/stmt/drop_table_stmt.cpp`

这里主要负责：

- 把 parser 结果转成内部 `Stmt`
- 检查要删除的表是否存在

### 3.2 Stmt 分发：接入 DROP TABLE

修改位置：

- `src/observer/sql/stmt/stmt.cpp`

把 `SCF_DROP_TABLE` 接到 `Stmt::create_stmt` 中，这样 SQL 解析之后才能真正生成 `DropTableStmt`。

### 3.3 Executor 层：新增 DropTableExecutor

修改位置：

- `src/observer/sql/executor/drop_table_executor.h`
- `src/observer/sql/executor/drop_table_executor.cpp`
- `src/observer/sql/executor/command_executor.cpp`

这里的作用是：

- 从 session 中拿到当前数据库
- 调用 `db->drop_table(table_name)`

因为 `drop-table` 是 DDL，所以它不需要走查询计划，而是直接由 executor 执行。

### 3.4 Db 层：实现真正的 drop_table

修改位置：

- `src/observer/storage/db/db.h`
- `src/observer/storage/db/db.cpp`

这是这道题的核心。

最终做的事情包括：

1. 检查表名是否合法
2. 检查目标表是否存在
3. 先收集该表的索引信息
4. 从 `opened_tables_` 中移除这张表
5. 析构表对象，关闭相关文件句柄
6. 删除磁盘文件：
   - `.table`
   - `.data`
   - `.lob`
   - 各个 `.index`

### 3.5 补全 DefaultHandler::drop_table

修改位置：

- `src/observer/storage/default/default_handler.cpp`

虽然主逻辑主要走 `Db`，但这里补上之后，整体接口链路更完整，也和 `create_table` 的结构更对称。

## 4. 这道题的关键难点

### 4.1 难点一：不是只删一份文件

表在 MiniOB 里不只是一个逻辑名字，它背后至少关联：

- 表元数据文件
- 数据文件
- 可能还有 lob 文件
- 多个索引文件

所以 `DROP TABLE` 不能理解成“删个表对象”就结束，而是要把一整组资源一起清掉。

### 4.2 难点二：删除顺序

这里顺序很重要。

先收集索引信息，再析构表对象，再删磁盘文件，原因是：

- 表对象析构前还持有文件句柄
- 析构后再从表元数据里取索引列表就不方便了

所以必须先保存好需要删除的索引文件名，再进行后续清理。

### 4.3 难点三：删除后状态要一致

题目不只是要求“删掉”，还要求：

- 删除后原表不能访问
- 同名表能重新创建

这说明：

- 内存中的 opened table 状态
- 磁盘上的元数据和数据文件

都必须同步清理干净。

## 5. 最终改动的核心文件

- `src/observer/sql/stmt/drop_table_stmt.h`
- `src/observer/sql/stmt/drop_table_stmt.cpp`
- `src/observer/sql/stmt/stmt.cpp`
- `src/observer/sql/executor/drop_table_executor.h`
- `src/observer/sql/executor/drop_table_executor.cpp`
- `src/observer/sql/executor/command_executor.cpp`
- `src/observer/storage/db/db.h`
- `src/observer/storage/db/db.cpp`
- `src/observer/storage/default/default_handler.cpp`

## 6. 手工验证要点

```sql
CREATE TABLE drop_table_test(id int, num int);
INSERT INTO drop_table_test VALUES (1, 10);
CREATE INDEX idx_num ON drop_table_test(num);

DROP TABLE drop_table_test;
INSERT INTO drop_table_test VALUES (2, 20);
CREATE TABLE drop_table_test(id int, num int);
DROP TABLE not_exist_table;
```

验证点：

- `DROP TABLE` 成功
- 删除后不能继续插入
- 可以重建同名表
- 删除不存在的表失败

## 7. 汇报时可以怎么讲

你可以把这题概括成三句：

1. `drop-table` 是典型 DDL，主链是 `Stmt -> Executor -> Db`，不是查询计划。
2. 真正要做的是把表对象、元数据文件、数据文件和索引文件一起清理干净。
3. 这题的关键不是语法，而是删除顺序和清理的一致性。
