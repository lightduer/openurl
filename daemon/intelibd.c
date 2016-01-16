#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>

#define PID_FILE "/tmp/openurl.pid"

#define sys_debug(format,args...)\
{\
	if(debug)\
		syslog(LOG_INFO,format,## args);\
}

int reload = 0;
int stop = 0;
int debug = 0;

static int save_pid()
{
	pid_t pid;
	int ret = 0;
	char buff[64] = {0,};
	FILE *fp = fopen(PID_FILE,"w+");
	if(fp == NULL){
		ret = -1;
		goto out;
	}
	pid = getpid();
	sprintf(buff,"%d",pid);
	fwrite(buff,strlen(buff),1,fp);
	fclose(fp);
out:
	return ret;
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
void *worker(void *data){
	while(!stop){
		sys_debug("worker is working\n");
		sleep(3);
	}
	return NULL;
}
int start()
{
	pthread_t tid;
	sigset_t bset,oset;
	siginfo_t info;
	daemon(0,0);
	save_pid();
	sigemptyset(&bset);
	sigaddset(&bset,SIGHUP);
	sigaddset(&bset,SIGTERM);
	sigaddset(&bset,SIGUSR1);
	sigaddset(&bset,SIGUSR2);
	pthread_create(&tid,NULL,worker,NULL);
	pthread_sigmask(SIG_BLOCK,&bset,&oset);
	while(!stop){
		if(reload){
		}
		if(sigwaitinfo(&bset,&info) != -1){
			do_signal(info.si_signo);
		}
	}
	pthread_join(tid,NULL);	

}
int sendsignal(int signo)
{
	char buff[64] = {0,};
	pid_t pid = read_pid();
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
