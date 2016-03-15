#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../include/url.h"

#define WORKERS_NUM 10
#define QUEUELEN 15
#define SOCKETNUM 5

#define TASK_IDLE 0
#define TASK_READY 1
#define TASK_CONNECT 2
#define TASK_DATA 4

#define SELECT_NUM 4

struct task
{
	int selectnum;//该socket轮训的次数没有数据到来过
	int state;
	int sock;
	struct url_item_request *data;
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
	pthread_mutex_t lock;//保护freenum 和IDLE状态
};

int master_hold[WORKERS_NUM];
struct worker workers[WORKERS_NUM];

void init_workers(void *(*worker_proc)(void *))
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
		workers[i].maxfd = -1;
		pthread_create(&workers[i].id,NULL,worker_proc,data);


	}

}
void exit_workers()
{
	int i;
	for(i = 0;i < WORKERS_NUM;i++){
		pthread_join(workers[i].id,NULL);
		pthread_mutex_destroy(&workers[i].lock);
	}
}
int send_http_request(struct task *t)
{
	int ret = 0,len,fd = t->sock;
	char *http_packet = NULL;
	struct url_item_request *data = t ->data;
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
	snprintf(http_packet,len,"GET %s HTTP/1.1\r\nHOST: %s\r\n\r\n",data->path,data->host);
	//printf("%.*s",len,http_packet);
	write(fd,http_packet,len);
	t->state = TASK_DATA;
	free(http_packet);
}
void do_work(int id)
{
	int i,fd = -1,state,flags,n,error;
	struct worker *man = &workers[id];
	struct sockaddr_in addr;
	struct timeval tv = {0,500};
	
	FD_ZERO(&man->read);
	FD_ZERO(&man->write);
	man->maxfd = -1;

	for(i = 0; i < QUEUELEN; i++){
		if(man->socketnum >= SOCKETNUM)
			break;
		state = man->tasks[i].state;
		if(state == TASK_READY ){
			memset(&addr,0,sizeof(struct sockaddr_in));
			fd = socket(AF_INET,SOCK_STREAM,0);
			flags = fcntl(fd,F_GETFL,0);
			fcntl(fd,F_SETFL,flags|O_NONBLOCK);
			addr.sin_family = AF_INET;
			addr.sin_port = htons(80);
			addr.sin_addr.s_addr = ((man->tasks[i]).data)->dst;
			man->tasks[i].sock = fd;
			man->socketnum++;
			if(connect(fd,(struct sockaddr*)&addr,sizeof(struct sockaddr))< 0){
				/*三次握手尚未完成*/
				man->tasks[i].state = TASK_CONNECT;
				FD_SET(fd,&man->read);
				FD_SET(fd,&man->write);
			}else{
				/*三次握手结束*/
				man->tasks[i].state = TASK_DATA;
				send_http_request(&man->tasks[i]);
				FD_SET(fd,&man->read);
			}
		}else {
			if(state == TASK_CONNECT || state == TASK_DATA){
				fd = state = man->tasks[i].sock;
				FD_SET(fd,&man->read);
			}
			if(state == TASK_CONNECT){
				FD_SET(fd,&man->read);
			}
		}
		if(fd > man->maxfd)
			man->maxfd = fd;
	}
	if(man->socketnum == 0)
		goto out;
	if(n = select(man->maxfd+1,&man->read,&man->write,NULL,NULL) < 0)
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
			// error
			}
			FD_CLR(fd,&man->write);
			send_http_request(&man->tasks[i]);
		}else if(state == TASK_DATA && FD_ISSET(fd,&man->read)){
			//read from socket
			char buff[65535] = {0};
			n = read(fd,buff,sizeof(buff));
			printf("%s\n",buff);
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
int queue_worker(struct url_item_request *data)
{
	int i,w;
	struct worker *man;
	w = select_worker();
	if(w == -1)
		goto out;
	man = &workers[w];
	pthread_mutex_lock(&man->lock);
	for(i = 0; i < QUEUELEN;i++){
		if(man->tasks[i].state == TASK_IDLE)
			break;
	}
	man->tasks[i].data = data;
	man->tasks[i].state = TASK_READY;
	(*man->freenum)--;
	pthread_mutex_unlock(&man->lock);
out:
	return w;
}
