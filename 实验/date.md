# date

## 1. 题目目标

在 MiniOB 现有 `int / float / char` 类型的基础上，新增 `date` 类型支持，并完成下面这些功能：

- `CREATE TABLE` 时支持 `date` 字段
- `INSERT` 时支持合法日期写入
- 非法日期输入返回 `FAILURE`
- `WHERE` 条件中支持日期比较
- `date` 字段可以建立索引
- 查询输出时按标准日期格式显示

这道题本质上不是“加一个关键字”，而是要为 MiniOB 新增一种完整的数据类型。

## 2. 总体思路

参考题目思路里的层次，这道题要从前到后打通整条链路：

1. Parse 层识别 `date`
2. 类型系统识别 `DATES`
3. 插入时把字符串日期转换成内部值
4. 存储层和索引层正确保存这个值
5. 查询层支持比较
6. 输出层支持格式化显示

所以真正的工作不是单点修改，而是把 `DATE` 接入 MiniOB 的完整处理流程。

## 3. 内部表示设计

这里最终采用的是：

- 日期内部按 4 字节整数 `YYYYMMDD` 存储

例如：

- `2020-01-21` -> `20200121`
- `2020-1-1` -> `20200101`

这样做有三个好处：

1. 存储简单，和 `INT` 一样容易落盘
2. 日期先后顺序和整数大小顺序一致
3. 比较和索引可以复用整数逻辑

这也是这道题最核心的设计点。

## 4. 具体实现

### 4.1 Parse 层：识别 date

修改位置：

- `src/observer/sql/parser/lex_sql.l`
- `src/observer/sql/parser/yacc_sql.y`

做的事情：

- 增加 `DATE` 关键字
- 把 `date` 映射到 `AttrType::DATES`

这样下面这条语句才能通过：

```sql
CREATE TABLE date_table(id int, u_date date);
```

### 4.2 类型系统：注册 DATES 和 DateType

修改位置：

- `src/observer/common/type/attr_type.h`
- `src/observer/common/type/attr_type.cpp`
- `src/observer/common/type/data_type.h`
- `src/observer/common/type/data_type.cpp`
- `src/observer/common/type/date_type.h`
- `src/observer/common/type/date_type.cpp`

做的事情：

- 新增 `AttrType::DATES`
- 注册 `DateType`
- 让系统知道 `DATES` 对应的比较、转换、格式化逻辑由 `DateType` 负责

### 4.3 DateType：日期解析、校验、比较、输出

核心文件：

- `src/observer/common/type/date_type.cpp`

主要完成 4 件事：

1. 解析字符串日期
2. 判断日期是否合法
3. 比较两个日期大小
4. 把内部整数格式转回日期字符串

### 4.4 日期合法性校验

支持的输入格式：

- `YYYY-MM-DD`
- `YYYY-M-D`
- `YYYY-M-DD`
- `YYYY-MM-D`

校验规则：

- 年必须是 4 位数字
- 月和日允许 1 到 2 位数字
- 月份范围必须在 `1~12`
- 日期必须在当月合法范围内
- 2 月 29 日必须满足闰年规则

例如：

- `2016-2-29` 合法
- `2017-2-29` 非法
- `2017-21-29` 非法
- `2017-12-32` 非法
- `2017-11-31` 非法

### 4.5 Value 层：让 DATES 真正能流动起来

修改位置：

- `src/observer/common/value.cpp`

这是这题特别容易漏掉的点。

虽然加了 `DateType`，但如果 `Value` 不支持 `DATES`，那 date 值仍然没法在系统里正常传递。
因此补齐了：

- `set_data` 对 `DATES` 的支持
- `set_value` 对 `DATES` 的支持
- `get_int` 对 `DATES` 的支持

这样记录中的 `date` 字段、比较表达式里的 `date` 值、最终输出时的 `date` 值，才能都走通。

### 4.6 类型转换：支持 CHARS -> DATES

修改位置：

- `src/observer/common/type/char_type.cpp`

这一点非常关键。

比如：

```sql
INSERT INTO date_table VALUES (1, '2020-01-21');
```

这里的 `'2020-01-21'` 在 parser 看来一开始是 `CHARS`，不是 `DATES`。
插入时系统会尝试把字面量类型转换成字段类型，所以必须支持：

- `CHARS -> DATES`

因此补了：

- `CharType::cast_to` 到 `DATES`
- `CharType::cast_cost` 到 `DATES`

没有这一步时，典型现象就是：

- `CREATE TABLE ... date` 成功
- 但所有合法日期 `INSERT` 都失败

### 4.7 比较和索引

比较方面：

- 因为日期内部就是 `YYYYMMDD`
- 所以大小比较直接按整数比较即可

索引方面：

- `date` 字段建立索引时，本质上也是按整数编码

修改位置：

- `src/observer/storage/common/codec.h`

需要让 `Codec` 支持 `AttrType::DATES`，这样 `CREATE INDEX ... ON date_field` 才能稳定工作。

## 5. 这道题的关键难点

### 5.1 难点一：不是 parser 写完就结束

这题最容易误判的地方是：

- `date` 关键字识别成功了
- `CREATE TABLE` 也成功了
- 以为就做完了

实际上真正难的是后面的：

- 插入
- 比较
- 索引
- 输出

如果后面几层没补齐，`DATE` 只是“看起来存在”，并不能真正使用。

### 5.2 难点二：类型转换

题目思路里提到这题的难点在“和其他类型的交互”，这一点和实际实现完全一致。

真正的难点不是日期本身，而是：

- 字符串日期怎么转成 `DATE`
- 比较时怎么决定谁转成谁
- 插入和查询里都要走正确的转换方向

### 5.3 难点三：非法输入的处理

题目要求明确提到：

- 非法 `date` 输入要返回 `FAILURE`

所以不仅非法 `INSERT` 要失败，像：

```sql
SELECT * FROM date_table WHERE u_date='2017-2-29';
```

这种非法日期条件输入，也应该失败。

这也是后期回归时最需要特别确认的点。

## 6. 最终改动的核心文件

- `src/observer/sql/parser/lex_sql.l`
- `src/observer/sql/parser/yacc_sql.y`
- `src/observer/common/type/attr_type.h`
- `src/observer/common/type/attr_type.cpp`
- `src/observer/common/type/data_type.h`
- `src/observer/common/type/data_type.cpp`
- `src/observer/common/type/date_type.h`
- `src/observer/common/type/date_type.cpp`
- `src/observer/common/type/char_type.cpp`
- `src/observer/common/value.cpp`
- `src/observer/storage/common/codec.h`

## 7. 手工验证要点

### 7.1 建表和建索引

```sql
CREATE TABLE date_table(id int, u_date date);
CREATE INDEX index_id ON date_table(u_date);
```

### 7.2 合法日期插入

```sql
INSERT INTO date_table VALUES (1,'2020-01-21');
INSERT INTO date_table VALUES (2,'2020-10-21');
INSERT INTO date_table VALUES (3,'2020-1-01');
INSERT INTO date_table VALUES (4,'2020-01-1');
INSERT INTO date_table VALUES (5,'2019-12-21');
INSERT INTO date_table VALUES (6,'2016-2-29');
INSERT INTO date_table VALUES (7,'1970-1-1');
INSERT INTO date_table VALUES (8,'2000-01-01');
INSERT INTO date_table VALUES (9,'2038-1-19');
```

### 7.3 比较测试

```sql
SELECT * FROM date_table WHERE u_date > '2020-1-20';
SELECT * FROM date_table WHERE u_date < '2019-12-31';
SELECT * FROM date_table WHERE u_date = '2020-1-1';
```

### 7.4 非法输入

```sql
SELECT * FROM date_table WHERE u_date='2017-2-29';
INSERT INTO date_table VALUES (10,'2017-2-29');
```

验证点：

- 都应该 `FAILURE`

## 8. 汇报时可以怎么讲

你可以把这题总结成三句：

1. `date` 题不是简单加关键字，而是给 MiniOB 增加一种完整的数据类型。
2. 我采用 `YYYYMMDD` 的整数编码，让日期的存储、比较和索引都能复用整数逻辑。
3. 这题最难的部分其实是类型转换和非法输入处理，而不是日期解析本身。
