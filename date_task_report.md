# MiniOB Date 题实现报告

## 1. 题目目标

这道题的目标是让 MiniOB 支持 `DATE` 类型，至少完成下面几件事：

1. 支持建表时声明 `date` 字段。
2. 支持插入合法日期。
3. 支持拒绝非法日期。
4. 支持 `where` 条件中的日期比较。
5. 支持查询结果按标准格式输出日期。

公开测试文件是 [test/case/test/primary-date.test](test/case/test/primary-date.test)。

## 2. 现有仓库中已经完成的部分

在当前分支里，下面这些基础接线已经存在：

1. 词法和语法里已经加入 `DATE_T`。
2. `AttrType` 中已经加入 `DATES`。
3. `DataType` 工厂已经注册 `DateType`。
4. 已经新增了 `date_type.h` / `date_type.cpp` 的基本骨架。

也就是说，这题不是从零开始，而是“最后关键链路还没打通”。

## 3. 本次真正补完的核心问题

### 3.1 日期字符串解析过于严格

原来的实现只接受固定长度的 `YYYY-MM-DD`，所以会错误拒绝这些测试数据：

- `2020-1-01`
- `2020-01-1`
- `2016-2-29`

这和公开测试要求不一致。

### 3.2 Value 没有真正支持 DATES

虽然已经有 `AttrType::DATES`，但 `Value` 这个核心值对象还没有把 `DATES` 当作一种可存取的数据类型处理。

这会导致：

1. 记录中的 date 字段读出来后不能正确装进 `Value`。
2. `DateType::compare` 比较时拿不到正确整数值。
3. `DateType::to_string` 输出时拿不到正确日期值。

换句话说，表面上“支持了 date 类型”，实际上数据流还没跑通。

## 4. 本次修改内容

### 4.1 修改 `src/observer/common/type/date_type.cpp`

完成了下面几件事：

1. 重写日期解析逻辑，不再要求字符串长度必须是 10。
2. 改为按 `-` 分成三段解析：年、月、日。
3. 要求年份必须 4 位，月份和日期允许 1 到 2 位。
4. 增加纯数字校验，防止非法字符混入。
5. 保留闰年校验和每月最大天数校验。
6. 内部仍然使用 `YYYYMMDD` 的 4 字节整数编码。

这样做的好处是：

1. 能兼容公开测试里的 `2020-1-01` 和 `2020-01-1`。
2. 插入和比较都可以统一转成整数处理。
3. 日期先后顺序和整数大小顺序一致，比较实现很简单。

### 4.2 修改 `src/observer/common/value.cpp`

给 `Value` 补上了 `DATES` 支持，主要补了三处：

1. `set_data` 新增 `AttrType::DATES` 分支。
2. `set_value` 新增 `AttrType::DATES` 分支。
3. `get_int` 新增 `AttrType::DATES` 分支。

这三处补完之后，`DATE` 数据才能真正参与：

1. 记录读取。
2. 条件比较。
3. 输出格式化。

## 5. 当前实现的工作流程

现在整条链路是这样的：

1. SQL 解析阶段识别 `date` 类型，字段类型记为 `AttrType::DATES`。
2. 插入字符串时，`DateType::set_value_from_str` 调用日期解析函数。
3. 合法日期会被编码成整数 `YYYYMMDD`。
4. 记录中保存的是 4 字节整数。
5. 查询比较时，`DateType::compare` 直接比较两个整数大小。
6. 查询输出时，`DateType::to_string` 再把整数转回 `YYYY-MM-DD`。

例如：

- `2020-01-21` 会被编码成 `20200121`
- `2020-1-1` 会被编码成 `20200101`
- 比较 `20200121 > 20200120` 成立，所以日期比较天然成立

## 6. 为什么内部用整数而不是字符串

因为这个方案最适合 MiniOB 现有框架：

1. 存储只占 4 字节，和 `int` 一样简单。
2. 比较开销低，不需要运行时逐段比较年月日。
3. 对索引和记录系统都更友好。
4. 代码改动范围小，和现有 `Value` / `DataType` 体系契合。

## 7. 这次修改对应的关键文件

本次我直接修改了：

1. `src/observer/common/type/date_type.cpp`
2. `src/observer/common/value.cpp`

当前分支里原本已经存在并参与这题的相关文件有：

1. `src/observer/sql/parser/lex_sql.l`
2. `src/observer/sql/parser/yacc_sql.y`
3. `src/observer/common/type/attr_type.h`
4. `src/observer/common/type/attr_type.cpp`
5. `src/observer/common/type/data_type.h`
6. `src/observer/common/type/data_type.cpp`
7. `src/observer/common/value.h`
8. `src/observer/common/type/date_type.h`

## 8. 目前还没在这个 Windows 环境里完成的事

这次没有在当前终端里完成真正的编译和自动化测试，原因不是代码逻辑，而是环境问题：

1. 当前环境里 `cmake` 不在可用路径中。
2. `test/case/miniob_test.py` 在 Windows 下会因为 `os.setpgrp()` 报错。
3. 当前 `build_debug/bin/observer` 产物在这个环境里也不能直接运行。

因此，代码已经补完，但最终回归测试建议你在课程要求的 Linux / WSL / 容器环境里执行。

## 9. 建议你下一步怎么验证

建议按下面顺序验证：

1. 重新编译项目。
2. 跑公开测试 `primary-date`。
3. 手工再测几条 SQL。

推荐重点检查：

1. `INSERT INTO date_table VALUES (3,'2020-1-01');`
2. `INSERT INTO date_table VALUES (4,'2020-01-1');`
3. `INSERT INTO date_table VALUES (6,'2016-2-29');`
4. `SELECT * FROM date_table WHERE u_date>'2020-1-20';`
5. `INSERT INTO date_table VALUES (10,'2017-2-29');`
6. `INSERT INTO date_table VALUES (13,'2017-11-31');`

预期结果是：

1. 合法日期插入成功。
2. 非法日期插入失败。
3. 查询比较结果正确。
4. 输出格式统一为 `YYYY-MM-DD`。

## 10. 一句话总结

这道题真正的关键，不是只把 `DATE` 关键字接进 parser，而是把“字符串解析 -> 内部编码 -> 记录存取 -> 条件比较 -> 结果输出”整条链路打通。

本次修改完成的就是这条核心链路。
