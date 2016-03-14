
#include "../include/url.h"

#define TASKS_NUM 15
#define SOCKETS_NUM 5
#define TASK_IDLE 0
#define TASK_READY 1
#define TASK_CONNECT 2
#define TASK_DATA 4

#define SELECT_NUM 4

struct task
{
	int selectnum;
	int state;
	int sock;
	struct url_item_requeset *data;
};

struct worker
{
	fd_set read;
	fd_set write;
	int socketnum;
	int *freenum;
	struct task tasks[TASKS_NUM];
	pthread_mutex_t lock;
}

int master_hold[WORKERS_NUM];
struct worker workers[WORKERS_NUM];

static int select_worker()
{
	int i,tmp,who = -1;
	
	if((tmp = master_hold[0]) > 0)
		who = 0;
	for(i = 1;i < WORKERS_NUM;i++){
		if(tmp < master_hold[i])
			who = i;			
	}
	return who;
	
}

int queue_worker(struct url_item_requeset *data)
{
	int i,w;
	struct worker *man;
	w = select_worker();
	if(w == -1)
		goto out;
	man = &workers[w];
	pthread_mutex_lock(&man->lock);
	for(i = 0; i < TASKS_NUM;i++){
		if(man->tasks[i].state == TASK_IDLE)
			break;
	}
	man->tasks[i].state = TASK_READY;
	man->tasks[i].data = data;
	*free_num--;
	pthread_mutex_unlock(&man->lock);
out:
	return w;
}
void do_work(int i)
{
	int i;
	int flags;
	struct worker *man = &workers[i];
	pthread_mutex_lock(&man->lock);
	for(i = 0; i < TASKS_NUM; i++){
		if(socketnum >= SOCKETS_NUM)
			break;
		if(man->tasks[i].state == TASK_READY ){
			man->tasks[i].sock = socket(AF_INET,SOCK_STREAM,0);
			flags = fcntl(man->tasks[i].sock,F_GETFL,0);
			fcntl(man->tasks[i].sock,F_SETFL,flags|O_NONBLOCK);

		}
				
	}
	pthread_mutex_unlock(&man->lock);	
}
