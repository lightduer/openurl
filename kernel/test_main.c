
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/fs.h>

struct task_struct *tk;

int thread_work(void *data)
{
	while(!kthread_should_stop()){
		printk("test worker!\n");
		schedule_timeout_interruptible(1000);	
	}
	return 0;
}

static int __init test_main_init(void)
{
	tk = kthread_run(thread_work,NULL,"test_thread");	
	return 0;
}
static void __exit test_main_exit(void)
{
	
	kthread_stop(tk);
	return ;
}
MODULE_LICENSE("GPL v2");
module_init(test_main_init);
module_exit(test_main_exit);
