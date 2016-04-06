#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>

#include "inteli_sock_hash.h"
#include "inteli_thread.h"
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
			stop_thread();
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
	init_threads();
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
