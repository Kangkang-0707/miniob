# aggregation-func

## 1. 题目目标

在 MiniOB 中补齐基础聚合函数支持，要求至少覆盖：

- `count(*)`
- `count(col)`
- `min(col)`
- `max(col)`
- `avg(col)`
- `sum(col)`

同时要满足题目和测试里的约束：

- 聚合列和普通列不能混用，例如 `select id, count(num) from t;` 要失败
- `min(*)`、`max(*)`、`avg(*)` 这类非法写法要失败
- `count(*, num)`、`count()` 这类空参数或多参数场景要失败
- 不存在字段要失败

这道题真正考察的不是“把几个函数名识别出来”，而是把聚合从 parser、binder 到执行阶段整条链路理顺。

## 2. 总体思路

这题我最终采用的是“尽量复用现有聚合框架，只补语法入口和错误场景”的做法。

MiniOB 当前代码里本来就已经有一套聚合表达式和聚合器：

- `UnboundAggregateExpr`
- `AggregateExpr`
- `Count / Sum / Avg / Min / MaxAggregator`

所以主要工作不是重写聚合执行器，而是把下面几件事补完整：

1. 让聚合函数语法能够被识别
2. 让非法参数形式不要卡在 parser，而是进入语义层后返回 `FAILURE`
3. 让 binder 正确区分 `count(*)`、普通单参数聚合、以及非法参数场景
4. 保持和后续 `group by`、中等题聚合子查询共用同一套基础设施

## 3. 修改了哪些东西

### 3.1 语法层

修改位置：

- `src/observer/sql/parser/yacc_sql.y`

做的事情：

- 保留了普通聚合函数 `ID(expr)` 的解析
- 新增了对 `ID()` 和 `ID(expr1, expr2, ...)` 的接收能力

这里的关键点是：
`count()`、`count(*,num)` 这类错误写法如果直接在 parser 阶段报 `SQL_SYNTAX`，和题目预期不一致。
所以我把它改成“先解析出来，再在语义绑定阶段返回 `FAILURE`”。

具体策略是：

- `ID()` 解析成 `UnboundAggregateExpr(name, nullptr)`
- `ID(expr1, expr2, ...)` 如果参数个数不是 1，也构造一个空 child 的聚合表达式

这样 parser 不再提前拦截，后面 binder 会统一把它判成非法参数。

### 3.2 表达式对象安全性

修改位置：

- `src/observer/sql/expr/expression.h`

做的事情：

- 给 `UnboundAggregateExpr::copy()` 增加了空 child 保护
- 给 `UnboundAggregateExpr::value_type()` 增加了空 child 保护

原因很直接：
当 `count()` 或 `count(*,num)` 这种错误输入被解析成“空 child 的聚合表达式”以后，如果表达式对象内部还默认 `child_` 一定存在，就会导致空指针崩溃。

所以这里补的是稳定性，不是功能扩展。

### 3.3 绑定阶段

修改位置：

- `src/observer/sql/parser/expression_binder.cpp`

做的事情：

- 在 `bind_aggregate_expression` 里增加了空 child 检查

也就是：

- `count(*)`：child 是 `StarExpr`，特殊转换成常量 `1`
- `count(num)`：child 是普通字段表达式，正常绑定
- `count()`：child 为空，直接 `INVALID_ARGUMENT`
- `count(*,num)`：child 被标成非法参数场景，也直接 `INVALID_ARGUMENT`

这一步修完以后，原来会触发 `ABORTING` 的场景就能稳定变成 `FAILURE`。

## 4. 关键难点

### 4.1 难点一：错误输入应该是 FAILURE，不是 SQL_SYNTAX

最容易踩坑的点是：

- `count()` 和 `count(*,num)` 是错误输入
- 但题目更希望它们表现成执行失败，而不是语法解析失败

所以不能简单依赖 yacc 语法把它们挡在外面，而要把它们送到 binder 再判错。

### 4.2 难点二：空 child 会把聚合表达式炸掉

当我们故意让错误场景“先解析出来”后，就会带来新的风险：

- 表达式对象内部可能默认 child 一定存在
- binder 里也可能直接 `child_expr->type()`

这就是为什么后来补了：

- `expression.h` 的空指针保护
- `expression_binder.cpp` 的空 child 检查

### 4.3 难点三：尽量不动已经跑通的聚合执行主链

前面在中等题里已经补过聚合器逻辑，`AVG/MIN/MAX/COUNT` 主体是正常的。
这次如果再去大改聚合执行层，风险反而更大。

所以最终策略是：

- 不碰已有聚合执行器
- 只补 parser 和 binder 的错误场景收口

这样能最大程度避免影响已经通过的 `simple-sub-query`。

## 5. 手工验证

初始化：

```sql
CREATE TABLE aggregation_func(id int, num int, price float, addr char, birthday date);
INSERT INTO aggregation_func VALUES (1, 18, 10.0, 'abc', '2020-01-01');
INSERT INTO aggregation_func VALUES (2, 15, 20.0, 'abc', '2010-01-11');
INSERT INTO aggregation_func VALUES (3, 12, 30.0, 'def', '2021-01-21');
INSERT INTO aggregation_func VALUES (4, 15, 30.0, 'dei', '2021-01-31');
```

关键验证：

```sql
SELECT count(*) FROM aggregation_func;
SELECT count(num) FROM aggregation_func;
SELECT min(num) FROM aggregation_func;
SELECT max(num) FROM aggregation_func;
SELECT avg(num) FROM aggregation_func;
SELECT min(addr) FROM aggregation_func;
SELECT max(addr) FROM aggregation_func;

SELECT id, count(num) FROM aggregation_func;
SELECT min(*) FROM aggregation_func;
SELECT count(*,num) FROM aggregation_func;
SELECT count() FROM aggregation_func;
```

预期：

- 前面合法聚合全部成功
- 后面非法写法全部返回 `FAILURE`

## 6. 最终效果

这题最后实现出来的效果是：

- 基础聚合函数能正常执行
- 合法和非法场景区分清楚
- 错误参数场景不再崩溃
- 和中等题里已经做过的聚合子查询链路保持兼容

如果明天汇报这题，可以概括成一句话：

“我没有重写聚合执行器，而是在复用现有聚合框架的基础上，把聚合函数的语法入口、参数校验和错误场景稳定性补完整了。”
