#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <net/sock.h>
#include <net/ipv6.h>

#include "intel_netlink.h"

static struct sock* intel_sock = NULL;
static __u32 user_pid = 0;
static void kernel_receive(struct sk_buff *skb);

void intel_netlink_fini(void)
{
	user_pid = 0;
	netlink_kernel_release(intel_sock);
}

int intel_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {.input = kernel_receive,};
	intel_sock = netlink_kernel_create(&init_net, NETLINK_URL_INTEL,&cfg);
	if(!intel_sock)
		return -1;
	return 0;
}

void kernel_receive(struct sk_buff *skb)
{
	int len,len2 = sizeof(struct url_item);
	struct nlmsghdr *nlh = NULL;
	struct url_item *r;	
	if( skb == NULL )
		goto out;
	if(skb->len < sizeof(struct nlmsghdr))
		goto out;
	if((nlh = nlmsg_hdr(skb)) == NULL)
		goto out;
	if(nlh->nlmsg_len < sizeof(struct nlmsghdr))
		goto out;
	if(skb->len < nlh->nlmsg_len)
		goto out;
	printk("recevie a msg!\n");	
	switch(nlh->nlmsg_type){
		case MSG_ADD_URL:
			len = skb->len - NLMSG_LENGTH(0);
			r = (struct url_item *)NLMSG_DATA(nlh);
			printk("%s\n",r->host);
			while(len >= len2){
				r++;
				len -= len2;	
			}
			break;
		case MSG_USER_PID:
			user_pid = nlh->nlmsg_pid;
			break;
		case MSG_USER_CLOSE:
			user_pid = 0;
			break;
		default:
			break;
	}
out:
	return;
}

void netlink_intel_send(char *host,int hostlen,char *path,int pathlen,
	unsigned int ipaddr,int method)
{
	int size;
	unsigned char *old_tail = NULL;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	struct url_item *data;

	
	if(user_pid == 0)
		goto out;
	printk("user_pid is %d\n",user_pid);
	if(hostlen < 0 || hostlen > URL_HOST_LEN)
		goto out;
	if(pathlen < 0 || pathlen > URL_PATH_LEN)
		goto out;
	size = NLMSG_SPACE(sizeof(struct url_item));
	skb = alloc_skb(size, GFP_ATOMIC);
	old_tail = skb_tail_pointer(skb);	
	nlh = (struct nlmsghdr*)skb_put(skb,NLMSG_LENGTH(sizeof( struct url_item)));
	data = NLMSG_DATA(nlh);

	memset(data, 0, sizeof(struct url_item));
	memcpy(data->host,host,hostlen);
	data->host[hostlen] = '\0';
	printk("host:%s\n",data->host);
	memcpy(data->path,path,pathlen);
	data->path[pathlen] = '\0';

	data->dst = ipaddr;
	data->method = method;

	nlh->nlmsg_type = MSG_STUDY_URL;
	nlh->nlmsg_len = skb_tail_pointer(skb)- old_tail;
	netlink_unicast(intel_sock, skb, user_pid, MSG_DONTWAIT); 
out:
	return;
}

