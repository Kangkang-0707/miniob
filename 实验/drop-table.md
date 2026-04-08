# drop-table

## 目标

支持：

```sql
DROP TABLE table_name;
```

并满足这些行为：

- 可以删除空表和非空表
- 删除后原表不可再访问
- 同名表可以重新创建
- 删除不存在的表要失败
- 删除带索引的表时，要把索引文件一起清理掉

## 实现主线

`drop-table` 是 DDL，走的是 `stmt -> executor -> db` 这条线，而不是查询计划。

这次主要补了 4 层：

1. parser 之后的语义对象
2. command executor 的分发
3. `Db::drop_table`
4. `DefaultHandler::drop_table`

## 具体步骤

### 1. 新增 `DropTableStmt`

位置：

- `src/observer/sql/stmt/drop_table_stmt.h`
- `src/observer/sql/stmt/drop_table_stmt.cpp`

作用：

- 把 parser 解析出来的 `DropTableSqlNode` 转成内部 `Stmt`
- 在 `create` 阶段检查表是否存在

### 2. 在 `Stmt::create_stmt` 中接入

位置：

- `src/observer/sql/stmt/stmt.cpp`

补充：

- `SCF_DROP_TABLE -> DropTableStmt::create(...)`

这样 SQL 解析之后才能真正生成 `DROP_TABLE` 类型的语句对象。

### 3. 新增 `DropTableExecutor`

位置：

- `src/observer/sql/executor/drop_table_executor.h`
- `src/observer/sql/executor/drop_table_executor.cpp`

作用：

- 从 session 拿到当前数据库
- 调用 `db->drop_table(table_name)`

### 4. 在 `CommandExecutor` 中分发

位置：

- `src/observer/sql/executor/command_executor.cpp`

补充：

- `StmtType::DROP_TABLE` 分支

因为 `drop-table` 属于 DDL，不生成物理执行计划，而是直接走 executor。

### 5. 实现 `Db::drop_table`

位置：

- `src/observer/storage/db/db.h`
- `src/observer/storage/db/db.cpp`

这里是核心逻辑：

1. 检查表名是否合法
2. 检查表是否已打开
3. 先收集该表的索引名
4. 从 `opened_tables_` 中移除
5. `delete table`，让表对象关闭数据文件和索引文件句柄
6. 删除表相关文件：
   - `.table`
   - `.data`
   - `.lob`
   - 每个 `.index`

这样可以保证：

- 表元数据不会残留
- 数据文件不会残留
- 索引文件不会残留

### 6. 补 `DefaultHandler::drop_table`

位置：

- `src/observer/storage/default/default_handler.cpp`

虽然当前主流程更多直接走 `Db`，但这里原来是空实现，顺手补上后整体链路会更完整。

## 细节考虑

### 为什么要先记录索引名再删表对象

因为 `delete table` 后就没法再通过 `table_meta()` 取索引列表了，所以要先把索引文件名依赖的信息拿出来。

### 为什么先 `delete table` 再删磁盘文件

因为表对象内部持有数据文件和索引文件句柄，先析构更稳，避免“文件还开着就删”的问题。

### 为什么 `DROP TABLE` 后还要 `sync`

`CommandExecutor` 里原本就会对 DDL 执行后做一次 `db->sync()`，这样元数据和日志状态更一致。

## 手工验证建议

```sql
CREATE TABLE t(id int, num int);
INSERT INTO t VALUES (1, 10);
CREATE INDEX idx_num ON t(num);
DROP TABLE t;
INSERT INTO t VALUES (2, 20);
CREATE TABLE t(id int, num int);
DROP TABLE not_exist_table;
```

预期：

- 第一次 `DROP TABLE t` 成功
- 删除后再插入失败
- 重建同名表成功
- 删除不存在的表失败
