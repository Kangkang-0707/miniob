# big-order-by 实现思路

## 目标

`big-order-by` 要支持 `SELECT ... ORDER BY ...`，用于对查询结果按一个或多个字段排序。根据 MiniOB 题目要求，本轮只支持字段排序，不支持 `ORDER BY 1`、表达式排序、`LIMIT`、窗口函数等扩展。

支持的语法包括：

```sql
SELECT * FROM t ORDER BY id;
SELECT * FROM t ORDER BY id ASC;
SELECT * FROM t ORDER BY id DESC;
SELECT * FROM t ORDER BY id DESC, score ASC, name DESC;
SELECT * FROM t1, t2 WHERE t1.id=t2.id ORDER BY t1.score DESC, t2.age ASC;
```

默认排序方向是 `ASC`。

## 总体链路

实现仍然走 MiniOB 原有的四段式链路：

1. `lex_sql.l` / `yacc_sql.y` 识别 `ORDER BY` 语法。
2. `parse_defs.h` 把 order-by 字段列表保存到 `SelectSqlNode`。
3. `SelectStmt::create` 使用 `ExpressionBinder` 把字段名绑定成真实 `FieldExpr`。
4. 逻辑计划中插入 `OrderByLogicalOperator`，物理计划中生成 `OrderByPhysicalOperator`。

执行计划位置是：

```text
Project
  OrderBy
    GroupBy / Predicate / Join / TableGet
```

也就是先从下层算子拿到完整 tuple 排序，再由 `Project` 输出用户真正 select 的列。这样 `SELECT name FROM t ORDER BY id` 这类 order 字段不在投影列表里的语句也能正确工作。

## Parser 改动

### 词法

在 `lex_sql.l` 中新增：

- `ORDER`
- `ASC`

`DESC` 原来已经用于 `DESC table`，可以复用为降序关键字。

### 语法

在 `yacc_sql.y` 中把原来的：

```yacc
SELECT expression_list FROM from_clause where group_by
```

扩展为：

```yacc
SELECT expression_list FROM from_clause where group_by order_by
```

新增规则：

```yacc
order_by:
    /* empty */
  | ORDER BY order_by_list

order_by_list:
    order_by_item
  | order_by_item COMMA order_by_list

order_by_item:
    rel_attr
  | rel_attr ASC
  | rel_attr DESC
```

这里复用已有 `rel_attr`，所以天然支持：

- `ORDER BY id`
- `ORDER BY t.id`

不支持表达式排序是有意收窄边界，因为题目只要求字段名即可。

## AST 设计

新增：

```cpp
struct OrderBySqlNode
{
  RelAttrSqlNode attr;
  bool           asc = true;
};
```

并在 `SelectSqlNode` 中增加：

```cpp
vector<OrderBySqlNode> order_by;
```

这样 parser 阶段只保存“字段 + 方向”，不提前接触表元数据；表名、字段名是否存在仍然交给 binder/stmt 阶段判断，和 MiniOB 现有风格一致。

## 语义绑定

在 `SelectStmt::create` 中遍历 `select_sql.order_by`：

1. 把 `RelAttrSqlNode` 构造成 `UnboundFieldExpr`。
2. 调用已有 `ExpressionBinder::bind_expression`。
3. 要求每个 order-by 项只能绑定出一个字段。
4. 保存到 `SelectStmt::order_by_`，方向保存到 `order_ascs_`。

这种做法有两个好处：

- 表不存在、字段不存在、未限定字段在多表场景中无法确定，都会沿用已有 binder 的错误处理。
- 排序执行时直接使用 `Expression::get_value`，不用重复写取字段逻辑。

## 逻辑计划

新增 `OrderByLogicalOperator`：

- 类型为 `LogicalOperatorType::ORDER_BY`
- 保存 `order_by_expressions_`
- 保存 `order_ascs_`

在 `LogicalPlanGenerator::create_plan(SelectStmt *)` 中，插入顺序是：

1. table scan / join
2. predicate
3. group by
4. order by
5. project

关键点是 order-by 放在 project 之前。原因是 project 之后只剩输出列，如果 order 字段没有出现在 select 列表中就会丢失。

## 物理执行

新增 `OrderByPhysicalOperator`，采用阻塞式排序：

1. `open()` 打开子算子。
2. 不断调用子算子 `next()` 读取所有 tuple。
3. 对每一行：
   - 先计算所有排序 key。
   - 再用 `ValueListTuple::make` 把子 tuple 物化下来。
4. 使用 `stable_sort` 按 key 列表逐个比较。
5. `next()` 按排序后的数组顺序返回。

比较逻辑：

- 多个排序字段从左到右比较。
- 当前 key 相等时继续比较下一个 key。
- `ASC` 使用 `Value::compare < 0`。
- `DESC` 使用 `Value::compare > 0`。
- 所有 key 都相等时保持原相对顺序，避免无意义抖动。

`Value::compare` 已经覆盖 `int/float/char/date/null` 的比较语义，所以排序算子不需要重新实现类型比较。

## 为什么不用索引排序

本轮没有做索引顺序扫描优化，而是统一走内存排序，原因是：

- 题目更关注 SQL 语义正确性。
- 多字段、多表 join 后排序很难直接复用单表索引。
- 现有索引扫描主要服务谓词过滤，强行接入会扩大改动面，容易影响已通过的 `multi-index` 和 `big-query`。

后续如果优化性能，可以在 `ORDER BY` 字段刚好命中单表索引且没有复杂 join/group 的情况下改写成索引顺序扫描。

## 边界处理

- 默认升序：`ORDER BY id` 等价于 `ORDER BY id ASC`。
- 多列排序：按声明顺序逐列比较。
- 多表排序：要求字段名可绑定；多表未限定字段如果无法唯一确定，会返回失败。
- `NULL` 排序：复用 `Value::compare`，不单独定义一套特殊规则。
- 不支持 `ORDER BY 1`：题目明确不要求数字位置排序。
- 不支持 `ORDER BY col + 1`：语法层只接受 `rel_attr`，避免扩大表达式排序范围。
- 不支持 `LIMIT`：`big-order-by` 只做排序，不改变结果行数。

## 验收问答速记

老师问“你怎么实现 order by 的？”

可以回答：我从 parser 到执行链路补了一个独立排序算子。parser 解析 `ORDER BY 字段 [ASC|DESC]` 列表，AST 保存字段和方向；`SelectStmt` 阶段用 binder 把字段绑定成 `FieldExpr`；逻辑计划在 project 之前插入 `OrderByLogicalOperator`；物理计划用 `OrderByPhysicalOperator` 一次性读取子算子的所有 tuple，计算排序 key 后 `stable_sort`，再按排序后的顺序返回。

老师问“为什么 order by 放在 project 前面？”

可以回答：因为 SQL 允许按非输出列排序，比如 `select name from t order by id`。如果先 project，`id` 可能已经不在 tuple 里了；放在 project 前排序可以访问完整行，排序结束后再投影用户需要的列。

老师问“多字段升降序怎么处理？”

可以回答：每一行保存一组 key，比较时从第一个 key 开始；相等则看下一个 key。每个 key 对应一个 `asc` 标记，升序用 `Value::compare < 0`，降序用 `> 0`。

老师问“有没有影响之前题目？”

可以回答：没有改变 table scan、predicate、join、group by、project 的原语义，只是在 select 包含 order-by 时额外插入排序算子；没有 order-by 的 SQL 仍走原计划。比较和 NULL 处理复用现有 `Value::compare`，不会另起一套类型规则。

## 建议测试 SQL

```sql
drop table t_order_by;
drop table t_order_by_2;

create table t_order_by(id int, score float, name char);
create table t_order_by_2(id int, age int);

insert into t_order_by values(3, 1.0, 'a');
insert into t_order_by values(1, 2.0, 'b');
insert into t_order_by values(4, 3.0, 'c');
insert into t_order_by values(3, 2.0, 'c');
insert into t_order_by values(3, 4.0, 'c');
insert into t_order_by values(3, 3.0, 'd');
insert into t_order_by values(3, 2.0, 'f');

insert into t_order_by_2 values(1, 10);
insert into t_order_by_2 values(2, 20);
insert into t_order_by_2 values(3, 10);
insert into t_order_by_2 values(3, 20);
insert into t_order_by_2 values(3, 40);
insert into t_order_by_2 values(4, 20);

select * from t_order_by order by id;
select * from t_order_by order by id asc;
select * from t_order_by order by id desc;
select * from t_order_by order by score desc;
select * from t_order_by order by name desc;
select * from t_order_by order by id, score, name;
select * from t_order_by order by id desc, score asc, name desc;
select * from t_order_by where id=3 and name>='a' order by score desc, name;
select * from t_order_by,t_order_by_2 order by t_order_by.id,t_order_by.score,t_order_by.name,t_order_by_2.id,t_order_by_2.age;
select * from t_order_by, t_order_by_2 where t_order_by.id=t_order_by_2.id order by t_order_by.score desc, t_order_by_2.age asc, t_order_by.id asc, t_order_by.name;
```
