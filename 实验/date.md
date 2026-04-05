# Date 题实现记录

## 1. 题目目标

为 MiniOB 增加 `DATE` 类型支持，至少完成下面这些功能：

1. `CREATE TABLE` 时支持 `date` 字段。
2. `INSERT` 时支持合法日期写入。
3. 非法日期要拒绝插入。
4. `WHERE` 条件中支持日期比较。
5. 查询结果能够按标准格式输出日期。

## 2. 总体思路

这道题的本质不是只加一个关键字，而是新增一种完整的数据类型，让它进入 MiniOB 的整条处理链路：

1. SQL 语法识别 `date`
2. 类型系统识别 `DATES`
3. 插入时把字符串日期转成内部值
4. 记录和索引支持存储这个值
5. 查询时支持日期比较
6. 输出时把内部值转回日期字符串

## 3. 内部表示设计

日期内部使用 4 字节整数 `YYYYMMDD` 存储，例如：

- `2020-01-21` -> `20200121`
- `2020-1-1` -> `20200101`

这样设计的原因：

1. 占用空间小，和 `INT` 一样方便存储。
2. 日期先后顺序和整数大小顺序一致。
3. 比较和索引都可以直接复用整数逻辑。

## 4. 具体实现步骤

### 4.1 扩展 parser

需要让 SQL 解析器识别 `date`：

1. 在 `src/observer/sql/parser/lex_sql.l` 中增加 `DATE` 关键字。
2. 在 `src/observer/sql/parser/yacc_sql.y` 中把 `date` 映射到 `AttrType::DATES`。

完成后，下面语句才能通过：

```sql
CREATE TABLE date_table(id int, u_date date);
```

### 4.2 扩展类型系统

需要把 `DATE` 注册为新的类型：

1. 在 `src/observer/common/type/attr_type.h` 中增加 `AttrType::DATES`
2. 在 `src/observer/common/type/attr_type.cpp` 中补充类型字符串映射
3. 在 `src/observer/common/type/data_type.h` / `data_type.cpp` 中注册 `DateType`
4. 新增 `src/observer/common/type/date_type.h`
5. 新增 `src/observer/common/type/date_type.cpp`

这样系统才知道 `DATES` 对应的行为实现是谁。

### 4.3 实现 DateType

`DateType` 主要完成四件事：

1. 解析字符串日期
2. 校验日期是否合法
3. 比较两个日期大小
4. 输出标准日期字符串

核心逻辑在 `src/observer/common/type/date_type.cpp`。

#### 日期解析规则

支持：

- `YYYY-MM-DD`
- `YYYY-M-D`
- `YYYY-M-DD`
- `YYYY-MM-D`

约束：

1. 年必须是 4 位数字
2. 月和日允许 1 到 2 位数字
3. 必须恰好有两个 `-`
4. 月份必须在 `1~12`
5. 日期必须在对应月份合法范围内
6. 2 月 29 日必须满足闰年规则

#### 闰年判断

规则是：

1. 能被 400 整除是闰年
2. 能被 4 整除但不能被 100 整除是闰年

例如：

- `2000-2-29` 合法
- `2016-2-29` 合法
- `2017-2-29` 非法

### 4.4 补齐 Value 对 DATES 的支持

这是这道题最容易漏掉的部分。

虽然已经新增了 `DateType`，但如果 `Value` 不支持 `DATES`，那么 date 值在系统里仍然无法正确流动。

在 `src/observer/common/value.cpp` 中补了三处：

1. `set_data` 支持 `AttrType::DATES`
2. `set_value` 支持 `AttrType::DATES`
3. `get_int` 支持 `AttrType::DATES`

作用：

1. 记录中的 date 字段能正确读出
2. 查询比较时能拿到真实日期整数
3. 输出时能正确格式化

### 4.5 解决插入时的隐式转换

这是本题里最关键的实际 bug 点。

例如：

```sql
INSERT INTO date_table VALUES (1, '2020-01-21');
```

这里 `'2020-01-21'` 在 parser 看来先是 `CHARS`，不是 `DATES`。

插入时系统会尝试把“字面量类型”转换成“字段类型”。因此必须支持：

- `CHARS -> DATES`

最终在 `src/observer/common/type/char_type.cpp` 中补了：

1. `CharType::cast_to` 支持转成 `DATES`
2. `CharType::cast_cost` 支持到 `DATES` 的转换代价

没有这一步时，现象就是：

1. `CREATE TABLE ... date` 成功
2. 但是所有合法日期 `INSERT` 全部失败

### 4.6 处理比较时的转换方向

对于下面这种语句：

```sql
SELECT * FROM date_table WHERE u_date > '2020-1-20';
```

左边字段是 `DATES`，右边字面量是 `CHARS`。

系统必须优先选择：

- 把字符串转成 `DATE`

而不是：

- 把 `DATE` 字段转成字符串

所以需要通过 `cast_cost` 控制转换优先级，使比较逻辑走向正确。

### 4.7 补齐索引编码

公开测试里会先创建日期索引：

```sql
CREATE INDEX index_id ON date_table(u_date);
```

因此索引编码也必须支持 `DATES`。

在 `src/observer/storage/common/codec.h` 中补充了：

1. `Codec::encode_value` 支持 `AttrType::DATES`

因为 date 内部本来就是整数编码，所以这里直接按整数编码即可。

## 5. 关键细节和易错点

### 5.1 不能只做 parser

如果只改 parser，`CREATE TABLE` 可能成功，但插入、比较、索引都会出问题。

### 5.2 不能把日期当普通字符串存

如果直接存字符串：

1. 比较逻辑更复杂
2. 索引支持更麻烦
3. 和 MiniOB 现有定长字段机制不够契合

### 5.3 插入失败不一定是 DateType 本身错

本题最容易误判的点是：日期解析已经写对了，但插入仍然失败。  
真正原因可能是：

1. `Value` 不支持 `DATES`
2. `CHARS -> DATES` 隐式转换没接上
3. 索引编码没支持 `DATES`

### 5.4 合法日期与非法日期都要覆盖

只验证“能插入”不够，还要验证“该失败时确实失败”。

## 6. 手工测试内容

### 6.1 建表和建索引

```sql
CREATE TABLE date_table(id int, u_date date);
CREATE INDEX index_id ON date_table(u_date);
```

### 6.2 合法日期

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

### 6.3 比较测试

```sql
SELECT * FROM date_table WHERE u_date > '2020-1-20';
SELECT * FROM date_table WHERE u_date < '2019-12-31';
SELECT * FROM date_table WHERE u_date = '2020-1-1';
```

### 6.4 删除测试

```sql
DELETE FROM date_table WHERE u_date > '2012-2-29';
SELECT * FROM date_table;
```

### 6.5 非法日期

```sql
INSERT INTO date_table VALUES (10,'2017-2-29');
INSERT INTO date_table VALUES (11,'2017-21-29');
INSERT INTO date_table VALUES (12,'2017-12-32');
INSERT INTO date_table VALUES (13,'2017-11-31');
```

## 7. 最终结论

这道题真正完成的内容是：

1. 为 MiniOB 新增了 `DATE` 类型
2. 打通了从 SQL 解析到记录存储、索引、比较、输出的完整链路
3. 通过日期合法性校验保证错误输入不会进入系统
4. 通过整数编码保证比较与索引实现简洁可靠
