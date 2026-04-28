# big-write

## 1. 题目目标

big-write 关注大批量写入。相比普通 `INSERT` 一行一行写，批量写入更强调：

- 减少逐行函数调用开销
- 支持 `LOAD DATA`
- 适配 PAX/row record page 写入
- 保证写入后的数据能被普通查询读取

## 2. 总体思路

MiniOB 中批量写入主要围绕 `LOAD DATA` 和 `Chunk`：

1. parser 识别 `LOAD DATA INFILE ... INTO TABLE ...`
2. `LoadDataStmt` 检查表和文件参数
3. load executor 读取文件内容并组装成 `Chunk`
4. `Table::insert_chunk` 下发到表引擎
5. `HeapTableEngine::insert_chunk` 调用 `RecordFileHandler::insert_chunk`
6. record manager 按页批量写入

普通 insert 仍走 `Table::make_record` 和 `insert_record`，两条路径最终都落到 record 层。

## 3. 关键模块

- `LoadDataSqlNode`：保存文件名、分隔符、引用符
- `LoadDataStmt`：做文件可读性和参数校验
- `LoadDataExecutor`：读取外部文件并转换为内部值
- `Chunk` / `Column`：批量数据的列式中间表示
- `HeapTableEngine::insert_chunk`
- `RecordFileHandler::insert_chunk`
- `PaxRecordPageHandler::insert_chunk`

## 4. 关键难点

### 4.1 批量写入和普通记录格式要一致

无论是单行 insert 还是 load data，最终写入的数据必须能被同一套 `TableScan` 和 `RowTuple` 读出来。因此字段类型、长度、offset 不能各走一套规则。

### 4.2 只优化写入，不改变查询语义

big-write 不是新 SQL 语义。它只是把大量数据更快写进表里，写完之后普通 `SELECT`、聚合、JOIN 等功能都应该正常工作。

### 4.3 错误处理

文件不存在、分隔符参数错误、字段数量不匹配等场景应返回 `FAILURE`，不能让进程崩溃。

## 5. 常见测试

```sql
create table t(id int, name char(20), score int);
load data infile '/tmp/t.csv' into table t fields terminated by ',' enclosed by '"';
select count(*) from t;
select * from t where id = 10;
```

## 6. 验收问答速记

**问：big-write 和普通 insert 的区别？**
答：普通 insert 一次构造一条 record；big-write 通过 `Chunk` 批量组织数据，再由 record manager 批量写页，减少逐行开销。

**问：LOAD DATA 写完后为什么 SELECT 能正常读？**
答：因为最终仍写入表引擎管理的 record page，字段布局和 `TableMeta/FieldMeta` 一致。

**问：批量写入是否维护索引？**
答：当前实现主要面向 load data 的批量落盘路径，索引维护能力要看具体引擎支持；普通 insert/update 路径会维护索引。
