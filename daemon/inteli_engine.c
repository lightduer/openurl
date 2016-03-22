#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/sqlite3.h"
#include "../include/mpse.h"

#define URL_KEYWORD_DB "../db/keyword.db"

struct categoryinfo
{
	int index;
	int category;	
	int totalwords;
};
struct keywordinfo
{
	char *word;
	int iid;
	int cat_num;
	int *cat_index;
	int tmp_index;
};
struct engine
{
	int ref;
	int keywordnum;
	struct keywordinfo *kinfos;
	int categorynum;
	struct categoryinfo *cinfos;
	void *fsm;
	pthread_mutex_t lock /*= PTHREAD_MUTEX_INITIALIZER*/;
	pthread_cond_t cond /*= PTHREAD_COND_INITIALIZER*/;
};
struct keywordlist
{
	struct keywordinfo *keyword;
	struct keywordlist *next;
};
struct resinfo
{
	int last_state;
	struct engine * fsm;
	struct keywordlist *list;
};


pthread_mutex_t englock = PTHREAD_MUTEX_INITIALIZER;

struct engine *eng,*oldeng;

int init_engine_db(struct engine *e)
{
	int i,j,len,ret = 0;
	int sqlret;
	int count = 0;
	int keynum;
	int cat_num;
	char *sql;
	char *tmpptr;
	int *indexptr;
	struct keywordinfo *ktmp;
	int row,col;
	char **result;
	sqlite3 *keyword_db;
	int grouptype,groupid;
#define PER_CATEGORY_KEYWORD 50	
	int deta = 5*PER_CATEGORY_KEYWORD;

	sqlret = sqlite3_open(URL_KEYWORD_DB, &keyword_db);
	if(sqlret != SQLITE_OK)
		goto out;
	sql = sqlite3_mprintf("select *from urlkeyword");
	if(sql == NULL)
		goto dbclose_out;
	sqlret = sqlite3_get_table(keyword_db, sql, &result, &row, &col, NULL);
	if(sqlret != SQLITE_OK)
		goto dbclose_out;
	
	e->categorynum = row;
	e->keywordnum = deta;

	e->cinfos = (struct categoryinfo *)malloc(sizeof(struct categoryinfo)*(e->categorynum));
	e->kinfos = (struct keywordinfo *)malloc(sizeof(struct keywordinfo)*(e->keywordnum));
	if(e->cinfos == NULL || e->kinfos == NULL)
		goto mem_out;

	for(i = 1;i <= row;i++){
		keynum = 0;
		groupid = atoi(result[col*i+1]);
		grouptype = atoi(result[col*i+2]);
		e->cinfos[i-1].index = i;
		e->cinfos[i-1].category = (grouptype << 8 | groupid)&0x0000ffff;

		if(count + PER_CATEGORY_KEYWORD > e->keywordnum){
			e->keywordnum += deta;
			e->kinfos = (struct keywordinfo *)realloc(e->kinfos,e->keywordnum);
			if(e->kinfos == NULL)
				goto mem_out;
		}
		tmpptr = result[col*i+3];
		tmpptr = strtok(tmpptr,",");
		while(tmpptr != NULL){
			memset(&e->kinfos[count],0,sizeof(struct keywordinfo));
			e->kinfos[count].word = strdup(tmpptr);
			e->kinfos[count].tmp_index = i;
			e->kinfos[count].cat_num = 1;
			keynum++;
			count++;
			tmpptr = strtok(NULL,",");
		}
		e->cinfos[i-1].totalwords = keynum;
	}
	len = sizeof(int) * e->categorynum;
	indexptr = (int *)malloc(sizeof(int));
	//keynum,cat_num,count,indexptr
	keynum = 0;
	for(i = 0;i < count;i++){
		memset(indexptr,0,len);
		if(e->kinfos[i].word == NULL){
			continue;
		}
		keynum++;
		cat_num = 1;
		indexptr[cat_num-1] = e->kinfos[i].tmp_index;	
		for(j = i+1;j < count;j++){
			if(e->kinfos[j].word == NULL)
				continue;
			if(!strcmp(e->kinfos[i].word,e->kinfos[j].word)){
				cat_num++;
				indexptr[cat_num-1] = e->kinfos[j].tmp_index;
				free(e->kinfos[j].word);
				e->kinfos[j].word = NULL;
			}
		}
		e->kinfos[i].cat_num = cat_num;
		e->kinfos[i].cat_index = (int*)malloc(cat_num*sizeof(int));
		if(e->kinfos[i].cat_index == NULL)
			goto mem_out;
		for(j = 0;j < cat_num;j++){
			e->kinfos[i].cat_index[j] = indexptr[j];	
		}	
	}
	ktmp = e->kinfos;
	e->kinfos = (struct keywordinfo *)malloc(sizeof(struct keywordinfo)*keynum);
	if(e->kinfos == NULL){
		e->kinfos = ktmp;
		goto mem_out;
	}
	for(i = 0,j=0;i < count;i++){
		if(ktmp[i].word == NULL)
			continue;
		memcpy(&e->kinfos[j],&ktmp[i],sizeof(struct keywordinfo));
		j++;
	}
	free(ktmp);
	e->keywordnum = keynum;
	goto success;
mem_out:
	if(e->cinfos)
		free(e->cinfos);
	if(e->kinfos){
		if(e->kinfos[i].cat_index)
			free(e->kinfos[i].cat_index);
		for(i = 0;i < e->keywordnum;i++)
			if(e->kinfos[i].word)
				free(e->kinfos[i].word);
		free(e->kinfos);
	}
dbclose_out:

	sqlite3_close(keyword_db);
out:
	ret = -1;
success:
	return ret;
}

static struct engine *alloc_fsm()
{
	struct engine *e;
	e = (struct engine *)malloc(sizeof(struct engine));
	if(!e)
		goto out;
	if(init_engine_db(e) == -1){
		free(e);
		e = NULL;
		goto out;
	}
	pthread_mutex_init(&e->lock,NULL);
	pthread_cond_init(&e->cond,NULL);
out:
	return e;
}
static void free_fsm(struct engine *e)
{
	int i;
	if(e == NULL)
		goto out;
	if(e->cinfos)
		free(e->cinfos);
	for(i = 0;i < e->keywordnum;i++){
		free(e->kinfos[i].word);
		free(e->kinfos[i].cat_index);
	}
	free(e);
out:
	return;
}
int init_engine()
{
	printf("init_engine\n");
	int ret = 0;
	struct engine *fsm  = alloc_fsm();
	if(fsm == NULL){
		ret = -1;
		goto out;
	}
	pthread_mutex_lock(&englock);
	if(eng){
		oldeng = eng;
	}
	eng = fsm;
	pthread_mutex_lock(&fsm->lock);
	fsm->ref = 1;
	pthread_mutex_unlock(&fsm->lock);
	pthread_mutex_unlock(&englock);
out:
	return ret;
}
void release_engine()
{
	printf("release_engine\n");
	struct engine * fsm = NULL;
	pthread_mutex_lock(&englock);
	if(oldeng)
		fsm = oldeng;
	else if(eng)
		fsm = eng;
	if(fsm == NULL)
		goto out;
	pthread_mutex_lock(&fsm->lock);
	fsm->ref = 0;
	while(fsm->ref != 0)
		pthread_cond_wait(&fsm->cond,&fsm->lock);
	pthread_mutex_unlock(&fsm->lock);
	if(fsm == oldeng)
		oldeng = NULL;
	else
		eng = NULL;
out:
	pthread_mutex_unlock(&englock);
	free_fsm(fsm);
}
#if 0

int main()
{
	init_engine();
	release_engine();
	return 0;
}

#endif

void *get_fsm()
{
	//为worker建立资源
	struct engine *fsm;
	struct resinfo * resid = (struct resinfo *)malloc(sizeof(struct resinfo));
       if(resid == NULL)
	       goto out;
	fsm = resid->fsm;
	pthread_mutex_lock(&englock);
	fsm = eng;
	pthread_mutex_lock(&fsm->lock);
	fsm->ref++;
	pthread_mutex_unlock(&fsm->lock);
	pthread_mutex_unlock(&englock);
	resid->last_state = 0;
	resid->list = NULL;
out:
	return resid;
}
void release_fsm(void *resin)
{
	int flag = 0;
	struct keywordlist *tmp;
	//清除worker占用的资源
	struct resinfo *resid = (struct resinfo *)resin;
	struct engine *fsm = resid->fsm;
	tmp = resid->list;
	while(!tmp){
		resid->list = tmp->next;
		free(tmp);
		tmp = resid->list;
	}
	pthread_mutex_lock(&fsm->lock);
	if((--eng->ref) == 0)
		flag = 1;
	pthread_mutex_unlock(&fsm->lock);
	if(flag == 1)
		pthread_cond_signal(&fsm->cond);
	free(resid);
}

int match(void *priv,int index,int id,void *data)
{
	int same = 0;
	struct resinfo *res = ( struct resinfo *)data;
	struct keywordlist *list = res->list;
	struct keywordlist *last;
	struct keywordinfo *word;
	struct keywordinfo *matchword = (struct keywordinfo *)priv;
	while(!list){
		last = list;
		word = list->keyword;
		if(word->iid == id){
			same = 1;
			break;
		}
		list = list->next;
	}
	if(same == 1)
		goto out;
	list = (struct keywordlist *)malloc(sizeof(struct keywordlist));
	if(!list){
		same = 1;
		goto out;
	}
	list->keyword = matchword;
	list->next = NULL;
	last->next = list;	
out:
	return same;
}
int match_fsm(void *resid,unsigned char *text,int textlen)
{
	struct resinfo *res = (struct resinfo *)resid;
	return mpse_search(res->fsm,text,textlen,match,resid,&res->last_state);
}
int get_result(void *resid)
{
	int i,index;
	struct resinfo *res = (struct resinfo *)resid;
	struct keywordlist *list = res->list;
	struct keywordinfo *word;
	int *matchnum = (int*)malloc(sizeof(int)*res->fsm->categorynum);
	memset(matchnum,0,sizeof(int)*res->fsm->categorynum);
	while(!list){
		word = list->keyword;
		for(i = 0;i < word->cat_num;i++){
			matchnum[(word->cat_index)[i]]++;		
		}	
		list = list->next;
	}

	free(matchnum);
	return 0;
}

