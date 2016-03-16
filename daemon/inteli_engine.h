#ifndef __INTELI_ENG__
#define __INTELI_ENG__

#include <pthread.h>
#if 0
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
	struct categroyinfo *cinfos;
	void *fsm;
	pthread_mutex_t *lock;
	pthread_cond_t *cond;
};

#endif
extern int init_engine();
extern void release_engine();
extern void *get_fsm();
extern void release_fsm(void *data);
extern int match_fsm(void *fsm,unsigned char *text,int textlen,
		int (*match)(void *priv,int index,int id,void *data),
		void *data,int *current_state);
extern int get_result(void *data);
#endif
