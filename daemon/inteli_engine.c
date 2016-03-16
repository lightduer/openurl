#ifndef __INTELI_ENG__
#define __INTELI_ENG__

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

struct categoryinfo
{
	int index;
	int category;	
};
struct keywordinfo
{
	char *word;
	int cat_num;
	int *cat_index;
};
struct engine
{
	int ref;
	int keywordnum;
	struct keywordinfo *kinfos;
	int categorynum;
	struct categroyinfo *cinfos;
	void *fsm;
	pthread_mutex_t lock /*= PTHREAD_MUTEX_INITIALIZER*/;
	pthread_cond_t cond /*= PTHREAD_COND_INITIALIZER*/;
};

pthread_mutex_t englock = PTHREAD_MUTEX_INITIALIZER;

struct engine *eng,*oldeng;

static struct engine *alloc_fsm()
{
	struct engine *e;
	e = (struct engine *)malloc(sizeof(struct engine));
	if(!e)
		goto out;
	pthread_mutex_init(&e->lock,NULL);
	pthread_cond_init(&e->cond,NULL);
out:
	return e;
}
static void free_fsm(struct engine *fsm)
{
	if(fsm == NULL)
		goto out;
	free(fsm);
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
	fsm->ref--;
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
void *get_fsm(void *worker)
{
	//为worker建立资源
	struct engine *fsm;
	pthread_mutex_lock(&englock);
	fsm = eng;
	pthread_mutex_lock(&fsm->lock);
	fsm->ref++;
	pthread_mutex_unlock(&fsm->lock);
	pthread_mutex_unlock(&englock);
	return fsm;	
}
void release_fsm(void *worker,void *e)
{
	int flag = 0;
	//清除worker占用的资源
	struct engine *fsm = (struct engine *)e;	
	pthread_mutex_lock(&fsm->lock);
	if((--eng->ref) == 0)
		flag = 1;
	pthread_mutex_unlock(&fsm->lock);
	if(flag == 1)
		pthread_cond_signal(&fsm->cond);
}
int match_fsm(void *fsm,unsigned char *text,int textlen,
		int (*match)(void *priv,int index,int id,void *data),
		void *data,int *current_state)
{
	return 1;
}
int get_result(void *data)
{
	return 0;
}

#endif
