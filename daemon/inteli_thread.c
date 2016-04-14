#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "inteli_sock_hash.h"
#include "inteli_engine.h"
#include "../include/url.h"

#define WORKERS_NUM 1
#define QUEUELEN 15
#define SOCKETNUM 5

#define TASK_IDLE 0
#define TASK_READY 1
#define TASK_CONNECT 2
#define TASK_DATA 4

#define SELECT_NUM 4

static int stop = 0;

struct task
{
	int selectnum;//该socket轮训的次数没有数据到来过
	int state;
	int sock;
	void *resid;
	struct url_item *data;
};

struct worker
{
	pthread_t id;
	fd_set read;
	fd_set write;
	int maxfd;
	int socketnum;//占用了多少个socket资源，资源总数是SOCKETNUM
	int *freenum;//有多少个空闲的queue buff,QUEUE最多缓存QUEUELEN
	struct task tasks[QUEUELEN];
	pthread_cond_t cond;
	pthread_mutex_t lock;//保护freenum 和IDLE状态
};

int master_hold[WORKERS_NUM];
struct worker workers[WORKERS_NUM];

static int send_http_request(struct task *t)
{
	int ret = 0,len,fd = t->sock;
	char *http_packet = NULL;
	struct url_item *data = t ->data;
#define HTTP_LEN_EXTRA 26
	len = strlen(data->host) + strlen(data->path) + HTTP_LEN_EXTRA;
	/*
	 *GET_PATH_HTTP/1.1\r\n
	 *HOST:_HOST\r\n
	 * \r\n
	 * */
	while((http_packet = (char *)malloc(sizeof(char)*(len))) == NULL){
		sleep(1);
	}
	snprintf(http_packet,len,"GET %s HTTP/1.0\r\nHOST: %s\r\n\r\n",data->path,data->host);
	write(fd,http_packet,len);
	t->state = TASK_DATA;
	free(http_packet);
}

static void send_to_resulter(struct url_item *data );

static void clear_a_task(struct worker *man,struct task *tk)
{
	close(tk->sock);
	man->socketnum--;
	tk->data->type = get_result(tk->resid);
	send_to_resulter(tk->data);
	release_fsm(tk->resid);
	pthread_mutex_lock(&man->lock);
	memset(tk,0,sizeof(struct task));
	(*(man->freenum))++;
	pthread_mutex_unlock(&man->lock);
}
static void do_work(int id)
{
	int i,fd = -1,state,flags,n,error;
	struct worker *man = &workers[id];
	struct sockaddr_in addr;
	struct timeval tv = {1,0};
	struct timespec ts;
	
	ts.tv_sec = time(NULL)+1;
	ts.tv_nsec = 0;
	FD_ZERO(&man->read);
	FD_ZERO(&man->write);
	man->maxfd = -1;
	
	pthread_mutex_lock(&man->lock);	
	/*当freenum == QUEUELEN 说明此工作线程，没有任务，需要睡眠等待超时，或者消息*/
	while((*(man->freenum)) == QUEUELEN)
		pthread_cond_wait(&man->cond,&man->lock);//超时时间
	pthread_mutex_unlock(&man->lock);

/*	if(isstop)
		goto out;
		*/
	/*below code: 有任务*/	
	for(i = 0; i < QUEUELEN; i++){
		state = man->tasks[i].state;
		if(state == TASK_READY ){
			if(man->socketnum >= SOCKETNUM)
				continue;
			memset(&addr,0,sizeof(struct sockaddr_in));
			fd = socket(AF_INET,SOCK_STREAM,0);/*close 代码:<file:lineno> = <>*/
			flags = fcntl(fd,F_GETFL,0);
			fcntl(fd,F_SETFL,flags|O_NONBLOCK);
			addr.sin_family = AF_INET;
			addr.sin_port = htons(80);
			addr.sin_addr.s_addr = ((man->tasks[i]).data)->dst;
			man->tasks[i].sock = fd;
			man->tasks[i].resid = get_fsm();/*release_fsm <file:lineno> = <>*/
			(man->socketnum)++;
			if(connect(fd,(struct sockaddr*)&addr,sizeof(struct sockaddr))< 0){
				/*三次握手尚未完成*/
				printf("connect select\n");
				man->tasks[i].state = TASK_CONNECT;
				FD_SET(fd,&man->read);
				FD_SET(fd,&man->write);
			}else{
				/*三次握手结束*/
				printf("connnect ok!\n");
				man->tasks[i].state = TASK_DATA;
				send_http_request(&man->tasks[i]);
				FD_SET(fd,&man->read);
			}
		}else {/*TASK_IDLE,CONNECT,DATA*/
			fd = man->tasks[i].sock;
			if(state == TASK_CONNECT || state == TASK_DATA){
				FD_SET(fd,&man->read);
			}
			if(state == TASK_CONNECT){
				FD_SET(fd,&man->write);
			}
		}
		if(fd > man->maxfd)
			man->maxfd = fd;
	}
	
	if(n = select(man->maxfd+1,&man->read,&man->write,NULL,&tv) < 0)
		goto out;
	for(i = 0;i < QUEUELEN;i++){
		state = man->tasks[i].state;
		if(state != TASK_CONNECT && state != TASK_DATA)
			continue;
		fd = man->tasks[i].sock;
		if(state == TASK_CONNECT && (
			FD_ISSET(fd,&man->read)||FD_ISSET(fd,&man->write))){
			n = sizeof(int);
			if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&n) < 0 || error != 0){
				/* 三次握手失败*/
				clear_a_task(man,&man->tasks[i]);
				continue;
			}
			FD_CLR(fd,&man->write);
			send_http_request(&man->tasks[i]);
		}else if(state == TASK_DATA && FD_ISSET(fd,&man->read)){
			//read from socket
			man->tasks[i].selectnum = 0;
			char buff[1024] = {0};
			if((n = read(fd,buff,sizeof(buff))) == 0){
				clear_a_task(man,&man->tasks[i]);
			}else{
				/*
				 * 1,注意这是TCP数据，不是HTTP数据
				 * 2,注意HTTP数据需要转码
				 * */
				match_fsm(man->tasks[i].resid,buff,n);
			}

		}else if(state == TASK_CONNECT || state == TASK_DATA){
			(man->tasks[i].selectnum)++;
			if(man->tasks[i].selectnum > SELECT_NUM){
				clear_a_task(man,&man->tasks[i]);
			}
		}
	}
out:
	return;	
}

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
static int queue_worker(struct url_item *data)
{
	int i,w;
	struct worker *man;
	w = select_worker();
	if(w == -1){
		printf("select_worker error!\n");
		goto out;
	}
	man = &workers[w];
	pthread_mutex_lock(&man->lock);
	for(i = 0; i < QUEUELEN;i++){
		if(man->tasks[i].state == TASK_IDLE)
			break;
	}
	man->tasks[i].data = data;
	man->tasks[i].state = TASK_READY;
	(*(man->freenum))--;
	pthread_mutex_unlock(&man->lock);
	pthread_cond_signal(&man->cond);
out:
	return w;
}
static void *worker_proc(void *data)
{
	int id = *(int *)data;
	free(data);
	while(!stop){
		do_work(id);
	}
	return NULL;
}
static void init_workers()
{
	int i;
	int *data;

	for(i = 0;i < WORKERS_NUM;i++){
		data = (int *)malloc(sizeof(int));
		*data = i;
		FD_ZERO(&workers[i].read);
		FD_ZERO(&workers[i].write);
		workers[i].socketnum = 0;
		master_hold[i] = QUEUELEN;
		memset(&workers[i].tasks,0,sizeof(QUEUELEN * sizeof(struct task)));
		workers[i].freenum = &master_hold[i];
		pthread_mutex_init(&(workers[i].lock),NULL);
		pthread_cond_init(&(workers[i].cond),NULL);
		workers[i].maxfd = -1;
		pthread_create(&workers[i].id,NULL,worker_proc,data);
	}

}
static void exit_workers()
{
	int i;
	for(i = 0;i < WORKERS_NUM;i++){
		pthread_join(workers[i].id,NULL);
		pthread_mutex_destroy(&workers[i].lock);
		pthread_cond_destroy(&workers[i].cond);
	}
}

/////////////////////////////分割线///////////////////////////////////////////
/*****************************以下是结果下称逻辑******************************/

#define RESULT_MAX 5
struct resulter
{
	int valid_num;
	struct url_item *queue[RESULT_MAX];
	pthread_cond_t cond;
	pthread_mutex_t lock;//保护freenum 和IDLE状态
};

struct resulter rer;
pthread_t resulterid;


static void send_to_resulter(struct url_item *data )
{
	int send = 0;
	int isfull = 0;
	while(!send){
		pthread_mutex_lock(&rer.lock);
		if(rer.valid_num == RESULT_MAX){
			isfull = 1;	
		}else{
			rer.queue[rer.valid_num] = data;
			rer.valid_num++;
			send = 1;
		}
		pthread_mutex_unlock(&rer.lock);
		if(isfull == 1){
			pthread_cond_signal(&rer.cond);
			usleep(50*1000);
		}
	}
}

static void store_result()
{
	int i,j;
	int tmp_num = 0;
	struct iovec *iov;
	struct timeval now;
	struct timespec outtime;
	struct url_item *rt;
	struct url_item *tmp_queue[RESULT_MAX];

	pthread_mutex_lock(&rer.lock);
	if(rer.valid_num < RESULT_MAX){
		gettimeofday(&now,NULL);
		outtime.tv_sec = now.tv_sec + 1;
		outtime.tv_nsec = 0;
		pthread_cond_timedwait(&rer.cond,&rer.lock,&outtime);
	}
	if(rer.valid_num > 0){
		tmp_num = rer.valid_num;
		memcpy(&tmp_queue,&rer.queue,RESULT_MAX*sizeof(struct url_item *));
	}
	rer.valid_num = 0;
	pthread_mutex_unlock(&rer.lock);
	if(tmp_num == 0)
		goto out;
	iov = (struct iovec *)malloc(sizeof(struct iovec) *(tmp_num + 1));/*free code : <file,lineno> = <this,348>*/
	for(i = 0,j = 1;i < tmp_num;i++){
		if(tmp_queue[i]->type != 0){
			iov[j].iov_base = tmp_queue[i];
			iov[j].iov_len = sizeof(struct url_item);
			j++;
		} 
	}
	send_msg2(MSG_ADD_URL,(void*)iov,j);
	for(i = 0,j = 1;i < tmp_num;i++){
		findanddelete(tmp_queue[i]);
		free(tmp_queue[i]);
	}
	free(iov);
out:
	return;
}
static void *resulter_proc(void *data)
{
	while(!stop){
		store_result();
	}
	return NULL;
}

static void init_rer()
{
	rer.valid_num = 0;
	pthread_mutex_init(&rer.lock,NULL);
	pthread_cond_init(&rer.cond,NULL);
	pthread_create(&resulterid,NULL,resulter_proc,NULL);
}
static void exit_rer()
{
	pthread_join(resulterid,NULL);
	pthread_mutex_destroy(&rer.lock);
	pthread_cond_destroy(&rer.cond);
}

///////////////////////////////////////////////////////////////////////////////
static void *receiver_proc(void *data)
{
	struct url_item *request;
	while(!stop){
		request = NULL;
		request = (struct url_item *)malloc(sizeof(struct url_item));
		if(request == NULL){
			sleep(1);
			continue;
		}
		memset(request,0,sizeof(struct url_item));
		while(recv_msg(request) == -1){
			//处理超时情况
			if(!stop)
				usleep(500*1000);
			else{
				free(request);
				goto out;
			}
		}
		if(findandinsert(request) == -1){	
			free(request);
			continue;
		}
		if(queue_worker(request) == -1){
			findanddelete(request);
			free(request);
			sleep(1);
		}
	}
out:
	return NULL;
}

void stop_thread()
{
	stop = 1;
}

pthread_t rcvid;


void init_threads()
{
	pthread_create(&rcvid,NULL,receiver_proc,NULL);
	init_workers();
	init_rer();
}


void exit_threads()
{
	pthread_join(rcvid,NULL);
	exit_workers();
	exit_rer();
}

#if 0

int main()
{
	init_hash();
	init_sock();
	init_threads();
	struct url_item *data;
	while(1){
		data = (struct url_item *)malloc(sizeof(struct url_item));
		data->method = 1;
		data->dst = 2;
		data->type =3;
		memcpy(data->host,"www.baidu.com",strlen("www.baidu.com"));
		send_to_resulter(data);
		sleep(1);
	}
	exit_threads();
	release_sock();

}

#endif
