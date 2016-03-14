
#define WORKERS_NUM 10

void init_workers(void *(*work_proc)(void *))
{
	for(i = 0;i < WORKERS_NUM;i++)
		pthread_create(&workerids[i],NULL,worker_proc,NULL);
}
void exit_workers()
{
	for(i = 0;i < WORKERS_NUM;i++)
		pthread_join(workerids[i],NULL);
}

