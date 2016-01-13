#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define PID_FILE "/tmp/openurl.pid"

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
static void remove_pid()
{
	char buff[64] = {0,};
	sprintf(buff,"rm -f %s",PID_FILE);
	system(buff);
	return;
}
int main()
{
	pid_t pid;
	daemon(0,0);
	save_pid();	
	pid = read_pid();
	remove_pid();
	return 0;
}
