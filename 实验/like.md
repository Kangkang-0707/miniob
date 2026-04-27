# like

## 1. 题目目标

在 MiniOB 中实现：

- `col like 'pattern'`
- `col not like 'pattern'`

并满足题目要求：

- 只考虑 `char` 类型字段
- `%` 表示匹配 0 到多个字符
- `_` 表示匹配恰好 1 个字符
- 不要因为扩展 `LIKE` 影响前面已经通过的题目

这题看起来像是一个简单语法扩展，但真正需要从 parser 一直接到比较表达式执行阶段。

## 2. 总体思路

这题最终采用的是“新增比较运算符，复用现有比较表达式”的做法。

也就是说，不新建一套单独的 `LIKE` 执行器，而是：

1. 在词法和语法中加入 `LIKE`、`NOT LIKE`
2. 在比较操作枚举里新增 `LIKE_OP`、`NOT_LIKE_OP`
3. 在 `ComparisonExpr` 的求值逻辑里实现模式匹配
4. 在 `SelectStmt` 归一化比较条件时，严格限制 `LIKE` 只接受 `char like char`

这样改动范围小，也更不容易破坏前面已经通过的 `date`、`join` 和子查询题。

## 3. 修改了哪些东西

### 3.1 比较操作枚举

修改位置：

- `src/observer/sql/parser/parse_defs.h`

做的事情：

- 新增 `LIKE_OP`
- 新增 `NOT_LIKE_OP`

这样后续 parser 和表达式层就能统一识别这两种比较类型。

### 3.2 词法与语法

修改位置：

- `src/observer/sql/parser/lex_sql.l`
- `src/observer/sql/parser/yacc_sql.y`

做的事情：

- 新增 `LIKE` 关键字
- 在 `comp_op` 中接入：
  - `LIKE`
  - `NOT LIKE`

这样下面这几种语句都能解析通过：

```sql
SELECT * FROM like_table WHERE name LIKE 'a%';
SELECT * FROM like_table WHERE name NOT LIKE 'a%';
```

### 3.3 执行层：模式匹配

修改位置：

- `src/observer/sql/expr/expression.cpp`

做的事情：

- 在 `ComparisonExpr` 中增加 `LIKE_OP / NOT_LIKE_OP` 的处理
- 新增 `like_match` 辅助函数

匹配策略是把 SQL 模式转换成正则：

- `% -> .*`
- `_ -> .`
- 其他正则特殊字符全部转义
- 最终用 `^...$` 做整串匹配

这样可以比较稳定地覆盖：

- 前缀匹配
- 后缀匹配
- 中间模糊匹配
- 单字符通配

### 3.4 语义约束：只允许 char like char

修改位置：

- `src/observer/sql/stmt/select_stmt.cpp`

做的事情：

- 在比较表达式归一化阶段，如果操作符是 `LIKE` 或 `NOT LIKE`
- 要求左右两边都必须是 `AttrType::CHARS`
- 否则直接返回 `INVALID_ARGUMENT`

这个限制非常重要，因为题目只要求 `char` 类型支持 `LIKE`。  
如果这里把 `int/float/date` 也放进来做隐式转换，虽然看起来“更智能”，但很容易破坏前面已经通过的比较与类型转换行为。

所以这里故意收得很紧。

## 4. 关键难点

### 4.1 难点一：不要把 LIKE 做成新的特殊分支系统

如果单独再搞一套 `like` 过滤逻辑，容易把代码改散。  
这题更好的方式是直接复用 `ComparisonExpr`，只把 `LIKE` 当成新的比较操作。

这样：

- parser 更统一
- 语义绑定更统一
- 执行逻辑也更统一

### 4.2 难点二：类型范围必须收紧

在前面的 `join-tables` 和 `simple-sub-query` 里，我们已经因为类型转换过宽踩过坑。  
所以这题不能再犯同样的问题。

尤其是：

```sql
SELECT * FROM like_table WHERE id LIKE '1%';
```

正确行为应该是 `FAILURE`，而不是偷偷把 `id` 转成字符串再去匹配。

因此最终的处理是：

- 只有 `char like char-pattern` 合法
- 其余一律失败

### 4.3 难点三：模式串不是普通字符串比较

`LIKE` 不是简单的 `=`：

- `%` 是任意长度匹配
- `_` 是单字符匹配

所以需要一套稳定的模式匹配逻辑。  
这里最后用的是“SQL 模式转正则”的实现，代码量小，也比较直观。

## 5. 手工验证

初始化：

```sql
CREATE TABLE like_table(id int, name char);
INSERT INTO like_table VALUES (1, 'abc');
INSERT INTO like_table VALUES (2, 'abb');
INSERT INTO like_table VALUES (3, 'xbc');
INSERT INTO like_table VALUES (4, 'abcc');
```

关键验证：

```sql
SELECT * FROM like_table WHERE name LIKE 'a%';
SELECT * FROM like_table WHERE name LIKE '%bc';
SELECT * FROM like_table WHERE name LIKE '_b_';
SELECT * FROM like_table WHERE name NOT LIKE 'a%';
SELECT * FROM like_table WHERE id LIKE '1%';
```

预期：

- `a%` 匹配 `abc`、`abb`、`abcc`
- `%bc` 匹配 `abc`、`xbc`
- `_b_` 匹配 `abc`、`abb`、`xbc`
- `NOT LIKE 'a%'` 匹配 `xbc`
- `id LIKE '1%'` 返回 `FAILURE`

## 6. 最终效果

这题最后实现出来的效果是：

- `LIKE / NOT LIKE` 语法完整可用
- `%` 和 `_` 都能正常工作
- 非 `char` 类型参与 `LIKE` 会正确失败
- 不会影响前面已经通过的 `date / drop-table / update / join / simple-sub-query`

如果明天汇报这题，可以概括成一句话：

“我把 LIKE 当成一种新的比较操作接进了现有比较表达式系统，同时严格限制只允许 char 类型参与，保证功能补上但不扩大已有类型转换风险。”
