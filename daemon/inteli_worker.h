
#ifndef __INTELI_WORKERS__
#define __INTELI_WORKERS__

void init_workers(void *(*work_proc)(void *));
void exit_workers();
void do_work(int i);
int queue_worker(struct url_item_request *);

#endif
