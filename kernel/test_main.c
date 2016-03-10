
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/fs.h>
#include <net/ip.h>
#include <linux/netfilter_ipv4.h>
#include "intel_netlink.h"

/*
#define TEST_URL "cn.bing.com"

struct task_struct *tk;

int thread_work(void *data)
{
	while(!kthread_should_stop()){
		printk("test worker!\n");
		netlink_intel_send(TEST_URL,strlen(TEST_URL),0,0,0,HTTP_GET);
		schedule_timeout_interruptible(1000);	
	}
	return 0;
}
*/
static unsigned int do_http(unsigned int hook,struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		int (*okfn)(struct sk_buff *))
{
	int i,datalen = 0;
	struct iphdr *ip;
	struct tcphdr *tcp;
	struct tcphdr tcph;
	char *http;
	char *host;
	char *path;
	char *url;
	char *end,*tmp;
	int hostlen = 0;	
	int pathlen = 0;
	
	if(skb_is_nonlinear(skb)){
		goto out;
	}	
	
	ip = ip_hdr(skb);
	if(ip->protocol != 6)
		goto out;
	tcp = skb_header_pointer(skb,ip_hdrlen(skb),sizeof(struct tcphdr),&tcph);
	if(tcp == NULL){
		goto out;
	}
	if(ntohs(tcp->dest) != 80){
		goto out;
	}
	if(tcp->syn == 1 || tcp->fin == 1 || tcp->rst == 1){
		goto out;
	}
	datalen = ntohs(ip->tot_len) - (sizeof(struct iphdr)+sizeof(struct tcphdr));
        if(datalen < 24){
	/*
	 * GET / HTTP/1.1\r\n
	 * HOST:  \r
	 * */
		goto out;
	}

	http = (char *)tcp + tcp->doff*4;
	if(strncmp(http,"GET",3)){
		goto out;
	}
	
	host = strstr(http,"Host: ");
	if(host == NULL){
		goto out;
	}
	path = http + strlen("GET ");
	for(i = strlen("GET ");i < datalen ; i++){
		if(http[i] != ' ')
			pathlen++;
		else
			break;
	}
	if(http[i] != ' ' || pathlen == 0)
		goto out;

	end = http + datalen;
	host += strlen("Host: ");
	for(tmp = host; tmp < end; tmp++){
		if(*tmp != '\r'){
			hostlen++;
		}else
			break;
	}
	if(*tmp != '\r'){
		goto out;
	}
	netlink_intel_send(host,hostlen,path,pathlen,ip->daddr,HTTP_GET);
out:
	return NF_ACCEPT;
}

static struct nf_hook_ops http_ops __read_mostly = 
{
	.hook = do_http,
	.pf = PF_INET,
//	.hooknum = NF_INET_FORWARD,
	.hooknum = NF_INET_LOCAL_OUT,
	.priority = (NF_IP_PRI_FILTER +1),
};

static int __init test_main_init(void)
{
	nf_register_hook(&http_ops);
	intel_netlink_init();
	//tk = kthread_run(thread_work,NULL,"test_thread");	
	return 0;
}
static void __exit test_main_exit(void)
{
	nf_unregister_hook(&http_ops);
//	kthread_stop(tk);
	intel_netlink_fini();	
	return ;
}
MODULE_LICENSE("GPL v2");
module_init(test_main_init);
module_exit(test_main_exit);
