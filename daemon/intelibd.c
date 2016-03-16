#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include "inteli_hash.h"
#include "inteli_worker.h"
#include "inteli_engine.h"
#include "../include/url.h"

#define PID_FILE "/tmp/intelibd.pid"

#define sys_debug(format,args...)\
{\
	if(debug)\
		syslog(LOG_INFO,format,## args);\
}

int reload = 0;
int stop = 0;
int debug = 0;
int fd = -1;

#define LOCKMODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

pthread_t rcvid;
pthread_t resulterid;

int netlink_sock;

static int lockfile(int fd)
{
	struct flock fl;
	fl.l_type = F_WRLCK;  
	fl.l_start = 0;
	fl.l_whence = SEEK_SET;
	fl.l_len = 0;
	return (fcntl(fd, F_SETLK, &fl)); 
}

static int save_pid()
{
	pid_t pid;
	int ret = 0;
	char buff[64] = {0,};
	fd = open(PID_FILE, O_RDWR | O_CREAT, LOCKMODE );
	if(fd < 0){
		ret = -1;
		goto out;
	}
	if (lockfile(fd) < 0){
		ret = -1;
		close(fd);
		goto out;
	}
	ftruncate(fd,0);
	pid = getpid();
	sprintf(buff, "%ld", (long)getpid());
	write(fd, buff, strlen(buff));
out:
	return ret;
}
static void remove_pid()
{
	if(fd >= 0)
		close(fd);
}
static pid_t read_pid()
{
	pid_t pid = 0 ;
	char buff[64] = {0,};
	FILE *fp = fopen(PID_FILE,"r");
	if(fp == NULL){
		goto out;
	}
	if(fread(buff,1,sizeof(buff),fp) == 0){
		fclose(fp);
		goto out;
	}
	pid = atoi(buff);
	fclose(fp);
out:
	return pid;
}
void do_signal(int signum){
	switch (signum){
		case SIGHUP:
			reload = 1;
			break;
		case SIGTERM:
			stop = 1;
			break;
		case SIGUSR1:
			if(debug == 0)
				debug = 1;
			else
				debug = 0;
			break;
		case SIGUSR2:break;
		default:
			     printf("sig number is %d\n",signum);
	}
	return;
}
int sendsignal(int signo);
int start();
void help();

static int init_sock();
static int init_threads();

static void exit_threads();
static void release_sock();

int main(int argc,char **argv)
{
	int ret = 0;
	char *option = argv[1];
	if(argc !=2){
		help();
		return 0;
	}
	if(!strncmp(option,"help",strlen("help"))){
		help();
	}else if(!strncmp(option,"stop",strlen("stop"))){
		ret = sendsignal(SIGTERM);
	}else if(!strncmp(option,"start",strlen("start"))){
		ret = start();
	}else if(!strncmp(option,"debug",strlen("debug"))){
		ret = sendsignal(SIGUSR1);
	}else if(!strncmp(option,"reload",strlen("reload"))){
		ret = sendsignal(SIGHUP);
	}
	return ret;
}
void *receiver_proc(void *data)
{
	struct url_item_request *request;
	while(!stop){
		sys_debug("receiver is working\n");
		request = NULL;
		request = (struct url_item_request *)malloc(sizeof(struct url_item_request));
		if(request == NULL){
			sleep(1);
			continue;
		}

		memset(request,0,sizeof(struct url_item_request));
		if(recv_msg(netlink_sock,request) == -1){
			free(request);
			usleep(500*1000);
			continue;
		}
	/*	if(findandinsert(request) == -1){	
			free(request);
			continue;
		}*/
		if(queue_worker(request) == -1){
			findanddelete(request);
			sleep(1);
		}

	}
	return NULL;
}

void *worker_proc(void *data)
{
	int id = *(int *)data;
	free(data);
	while(!stop){
		sys_debug("worker is working\n");
		do_work(id);
	}
	return NULL;
}
void *resulter_proc(void *data)
{
	while(!stop){
		sys_debug("result is working\n");
		//dumptable();
		sleep(3);
	}
	return NULL;
}


int start()
{
	int ret = 0;
	int pid_flg = 0,sock_flg = 0,hash_flg = 0,engine_flg = 0;
	sigset_t bset,oset;
	siginfo_t info;
//	daemon(0,0);
	if(-1 == save_pid()){
		ret = -1;
		goto out;		
	}
	pid_flg = 1;
	if(-1 == init_sock()){
		ret = -1;
		goto out;
	}
	sock_flg = 1;
	if(-1 == init_hash()){
		printf("hash init error!\n");
		ret = -1;
		goto out;		
	}
	hash_flg = 1;
	if(-1 == init_engine()){
		ret = -1;
		goto out;
	}
	engine_flg = 1;
	sigemptyset(&bset);
	sigaddset(&bset,SIGHUP);
	sigaddset(&bset,SIGTERM);
	sigaddset(&bset,SIGUSR1);
	sigaddset(&bset,SIGUSR2);
	if(-1 == init_threads()){
		ret = -1;
		goto out;
	}

	pthread_sigmask(SIG_BLOCK,&bset,&oset);

	while(!stop){

		if(reload){
			init_engine();
			release_engine();
		}
		if(sigwaitinfo(&bset,&info) != -1){
			do_signal(info.si_signo);
		}

	}
	exit_threads();
out:
	if(engine_flg)
		release_engine();
	if(hash_flg)
		release_hash();
	if(sock_flg)
		release_sock();
	if(pid_flg)
		remove_pid();
	return; 

}
int sendsignal(int signo)
{
	char buff[64] = {0,};
	pid_t pid = read_pid();
	printf("pid is %d\n",pid);
	if(pid == 0)
		return -1;
	sprintf(buff,"kill -%d %d",signo,pid);
	system(buff);
	return 0;
}
void help()
{
	printf("\n-----------------------------\n");
	printf("intelibd help\n");
	printf("intelibd start\n");
	printf("intelibd stop\n");
	printf("intelibd reload\n");
	printf("intelibd debug\n");
	printf("-----------------------------\n");
}
int send_msg(int sock, int type,void *data)
{
	int ret = -1;
	struct sockaddr_nl kpeer;                                                        
	socklen_t kpeer_len;
	struct nlmsghdr msghdr;
	struct url_item_result *result_msg=NULL;                                                                                                     

	if(data)
		result_msg=(struct url_item_result *)data;                                                                                           

	memset(&kpeer, 0, sizeof(kpeer));                                    
	kpeer.nl_family = AF_NETLINK;                                                                             
	kpeer.nl_pid = 0; 
	kpeer.nl_groups = 0;
	kpeer_len = sizeof(kpeer);                                                                                      
	memset(&msghdr, 0, sizeof(msghdr));                                                                              
	msghdr.nlmsg_flags = 0;
	msghdr.nlmsg_type = type;
	msghdr.nlmsg_pid = getpid();                                                                                           
	switch(type){       
		case MSG_USER_CLOSE:                                                     
			break;
		case MSG_ADD_URL:
			if(result_msg){
				msghdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct url_item_result));
				memcpy(&result_msg->hdr,&msghdr,sizeof(struct nlmsghdr));
				ret=sendto(sock, result_msg, msghdr.nlmsg_len,0,(struct sockaddr *)&kpeer, kpeer_len);
			}
			break;
		case MSG_USER_PID:
			msghdr.nlmsg_len = NLMSG_LENGTH(0);
			ret=sendto(sock, &msghdr, msghdr.nlmsg_len,
					0, (struct sockaddr *)&kpeer, kpeer_len);                      
			break;                                                                                            
		default:
			break;                                                                                                            
	}
	return ret;
}
int recv_msg(int sock,struct url_item_request *request)
{
	struct sockaddr_nl kpeer;
	int rcvlen=0,kpeer_len=0;
	struct url_item_request_deamon info;	
	if(request == NULL){
		rcvlen = -1;
		goto out;
	}
	memset(&kpeer, 0, sizeof(kpeer));
	kpeer.nl_family = AF_NETLINK;
	kpeer.nl_pid = 0;
	kpeer.nl_groups = 0;
	kpeer_len = sizeof(kpeer);
	rcvlen = recvfrom(sock, &info, sizeof(struct url_item_request_deamon), 0, (struct sockaddr*)&kpeer, (socklen_t *)&kpeer_len);
	if(rcvlen <=0){
		rcvlen = -1;
		goto out;
	}
	if (kpeer_len!= sizeof(struct sockaddr_nl) || kpeer.nl_pid != 0){
		rcvlen = -1;
		goto out;
	}
	request->method = info.data.method;
	request->dst = info.data.dst;
	strcpy(request->host,info.data.host);
	strcpy(request->path,info.data.path);
out:
	return rcvlen;
}
int init_sock()
{
	int ret = 0;
	struct sockaddr_nl local;
	struct timeval tm = {1,0};
	netlink_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_URL_INTEL);
	if(netlink_sock < 0){
		ret = -1;
		goto out;
	}
	if(setsockopt(netlink_sock,SOL_SOCKET,SO_RCVTIMEO,(const char *)&tm,sizeof(tm)) != 0){
		ret = -1;
		goto out;
	}
	memset(&local, 0, sizeof(local));
	local.nl_family = AF_NETLINK;
	local.nl_groups = 0;
	if(bind(netlink_sock, (struct sockaddr*)&local, sizeof(local)) != 0){
		ret = -1;
		goto out;
	}
	send_msg(netlink_sock,MSG_USER_PID,NULL);
out:
	return ret;
}

int init_threads()
{
	int i;
	int ret = 0;
	pthread_create(&rcvid,NULL,receiver_proc,NULL);
	init_workers(worker_proc);
	pthread_create(&resulterid,NULL,resulter_proc,NULL);
	return ret;
}
void exit_threads()
{
	int i;
	pthread_join(rcvid,NULL);
	exit_workers();
	pthread_join(resulterid,NULL);
	return;
}
void release_sock()
{
	send_msg(netlink_sock,MSG_USER_CLOSE,NULL);
	if(netlink_sock > 0)
		close(netlink_sock);
	return;
}
