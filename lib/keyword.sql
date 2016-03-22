BEGIN TRANSACTION;

create table keyword_group
(
	id	INT,
	name	CHAR(64),
	percent	INT,
	comment	CHAR(64)
);

insert into keyword_group (id, name, percent, comment) values (1, 'test', 70, 'for test');

create table keyword
(
	id		INT,
	group_name	CHAR(64),
	keyword		CHAR(128)
);

insert into keyword (id, group_name, keyword) values (1, 'test', 'hers');
insert into keyword (id, group_name, keyword) values (2, 'test', 'his');
insert into keyword (id, group_name, keyword) values (3, 'test', 'she');
insert into keyword (id, group_name, keyword) values (4, 'test', 'he');

COMMIT;
