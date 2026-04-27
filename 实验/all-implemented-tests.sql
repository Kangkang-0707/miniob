-- MiniOB implemented feature smoke tests
-- Run section by section if one failure should not stop later checks.

-- date
drop table date_t;
create table date_t(id int, d date);
insert into date_t values(1, '2024-01-01');
insert into date_t values(2, '2024-02-29');
insert into date_t values(3, '2024-12-31');
select * from date_t;
select * from date_t where d = '2024-02-29';
select * from date_t where d > '2024-01-31';

-- update
drop table update_t;
create table update_t(id int, score int, name char(20));
insert into update_t values(1, 10, 'alice');
insert into update_t values(2, 20, 'bob');
insert into update_t values(3, 30, 'cindy');
update update_t set score = 99 where id = 2;
update update_t set name = 'updated' where score >= 30;
select * from update_t;

-- aggregation
drop table agg_t;
create table agg_t(id int, score int, price float);
insert into agg_t values(1, 10, 1.5);
insert into agg_t values(2, 20, 2.5);
insert into agg_t values(3, 30, 3.5);
select count(*) from agg_t;
select count(score), sum(score), avg(score), min(score), max(score) from agg_t;
select avg(price), min(price), max(price) from agg_t;

-- join
drop table join_a;
drop table join_b;
create table join_a(id int, name char(20));
create table join_b(id int, score int);
insert into join_a values(1, 'alice');
insert into join_a values(2, 'bob');
insert into join_a values(3, 'cindy');
insert into join_b values(1, 90);
insert into join_b values(2, 80);
insert into join_b values(4, 70);
select * from join_a, join_b where join_a.id = join_b.id;
select join_a.name, join_b.score from join_a, join_b where join_a.id = join_b.id and join_b.score >= 80;

-- like
drop table like_t;
create table like_t(id int, name char(20));
insert into like_t values(1, 'alice');
insert into like_t values(2, 'bob');
insert into like_t values(3, 'alex');
insert into like_t values(4, 'carol');
select * from like_t where name like 'a%';
select * from like_t where name like '%o%';
select * from like_t where name not like 'a%';

-- simple sub query
drop table sub_a;
drop table sub_b;
create table sub_a(id int, score int);
create table sub_b(id int, score int);
insert into sub_a values(1, 10);
insert into sub_a values(2, 20);
insert into sub_a values(3, 30);
insert into sub_b values(10, 20);
insert into sub_b values(11, 30);
select * from sub_a where score in (select score from sub_b);
select * from sub_a where score not in (select score from sub_b);
select * from sub_a where score = (select max(score) from sub_b);

-- drop table
drop table drop_t;
create table drop_t(id int, v int);
insert into drop_t values(1, 1);
select * from drop_t;
drop table drop_t;
create table drop_t(id int, v int);
insert into drop_t values(2, 2);
select * from drop_t;

-- unique: single-column unique index
drop table unique_single;
create table unique_single(id int, v int);
create unique index uniq_single_id on unique_single(id);
insert into unique_single values(1, 10);
insert into unique_single values(2, 20);
-- expected: failure
insert into unique_single values(1, 30);
select * from unique_single;

-- unique: multi-column unique index
drop table unique_multi;
create table unique_multi(a int, b int, v int);
create unique index uniq_multi_ab on unique_multi(a, b);
insert into unique_multi values(1, 1, 10);
insert into unique_multi values(1, 2, 20);
insert into unique_multi values(2, 1, 30);
-- expected: failure
insert into unique_multi values(1, 1, 40);
select * from unique_multi;

-- unique: update conflict
drop table unique_update;
create table unique_update(id int, v int);
create unique index uniq_update_id on unique_update(id);
insert into unique_update values(1, 10);
insert into unique_update values(2, 20);
-- expected: failure, row id=2 should remain unchanged
update unique_update set id = 1 where v = 20;
select * from unique_update;

-- null: insert, compare, aggregate
drop table null_t;
create table null_t(id int not null, num int null, price float not null, birthday date null, name char(20) null);
insert into null_t values(1, 10, 1.5, '2024-01-01', 'alice');
insert into null_t values(2, null, 2.5, null, null);
insert into null_t values(3, 30, 3.5, '2024-03-03', 'cindy');
-- expected: failure
insert into null_t values(null, 40, 4.5, '2024-04-04', 'bad');
select * from null_t;
select * from null_t where num is null;
select * from null_t where num is not null;
select * from null_t where num = null;
select * from null_t where null is null;
select * from null_t where null is not null;
select count(*) from null_t;
select count(num), sum(num), avg(num), min(num), max(num) from null_t;

-- null: all-null aggregate
drop table null_all_t;
create table null_all_t(id int not null, v int null);
insert into null_all_t values(1, null);
insert into null_all_t values(2, null);
select count(*) from null_all_t;
select count(v), sum(v), avg(v), min(v), max(v) from null_all_t;

-- unique with null values: null index columns should not conflict
drop table unique_null;
create table unique_null(a int null, b int null, v int);
create unique index uniq_null_ab on unique_null(a, b);
insert into unique_null values(null, 1, 10);
insert into unique_null values(null, 1, 20);
insert into unique_null values(1, null, 30);
insert into unique_null values(1, null, 40);
insert into unique_null values(1, 1, 50);
-- expected: failure
insert into unique_null values(1, 1, 60);
select * from unique_null;

-- update-select: scalar aggregate subquery
drop table update_select_t;
drop table update_select_s;
create table update_select_t(id int not null, v int null);
create table update_select_s(x int not null);
insert into update_select_t values(1, 10);
insert into update_select_t values(2, 20);
insert into update_select_s values(7);
insert into update_select_s values(9);
update update_select_t set v = (select max(x) from update_select_s);
select * from update_select_t;

-- update-select: scalar filtered subquery
drop table update_select_t2;
drop table update_select_s2;
create table update_select_t2(id int not null, v int null);
create table update_select_s2(id int not null, x int not null);
insert into update_select_t2 values(1, 0);
insert into update_select_t2 values(2, 0);
insert into update_select_s2 values(1, 100);
insert into update_select_s2 values(2, 200);
update update_select_t2 set v = (select x from update_select_s2 where id = 1) where id = 2;
select * from update_select_t2;

-- update-select: empty subquery becomes NULL
drop table update_select_empty;
drop table update_select_empty_s;
create table update_select_empty(id int not null, v int null);
create table update_select_empty_s(x int not null);
insert into update_select_empty values(1, 10);
update update_select_empty set v = (select x from update_select_empty_s);
select * from update_select_empty;

-- update-select: empty subquery into not-null column should fail
drop table update_select_not_null;
drop table update_select_not_null_s;
create table update_select_not_null(id int not null, v int not null);
create table update_select_not_null_s(x int not null);
insert into update_select_not_null values(1, 10);
-- expected: failure
update update_select_not_null set v = (select x from update_select_not_null_s);
select * from update_select_not_null;

-- update-select: multi-row subquery should fail
drop table update_select_multi_row;
drop table update_select_multi_row_s;
create table update_select_multi_row(id int not null, v int null);
create table update_select_multi_row_s(x int not null);
insert into update_select_multi_row values(1, 10);
insert into update_select_multi_row_s values(1);
insert into update_select_multi_row_s values(2);
-- expected: failure
update update_select_multi_row set v = (select x from update_select_multi_row_s);
select * from update_select_multi_row;
