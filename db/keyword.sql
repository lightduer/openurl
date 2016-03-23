
create table urlkeyword(
	id 			INTEGER PRIMARY KEY AUTOINCREMENT,
	groupid 		INT,
	grouptype 		INT,
	keywordlist		CHAR(320)
);

insert into urlkeyword values(NULL,55,2,'王菲,狗仔,艳照');
insert into urlkeyword values(NULL,10,1,'百度,好友,浏览器');
insert into urlkeyword values(NULL,13,1,'王菲,好友,人人');

--insert into urlkeyword values(NULL,55,2,'nihao,renren');
--insert into urlkeyword values(NULL,13,1,'baidu'); 
