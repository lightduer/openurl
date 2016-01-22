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

static struct sock* intel_sock = NULL;
static __u32 use_pid = 0;

static void kernel_receive(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = NULL;
	
	if( skb == NULL )
		goto out;
	if(skb->len < sizeof(struct nlmsghdr))
		goto out;
	if((nlh = nlmsg_hdr(skb) == NULL))
		goto out;
	if(nlh->nlmsg_len < sizeof(struct nlmsghdr))
		goto out;
	if(skb->len < nlh->nlmsg_len)
		goto out;
	
	switch(nlh->nlmsg_type){
		case MSG_URL_RESULT:
			break;
		case MSG_USER_PID:
			use_pid = nlh->nlmsg_pid;
			break;
		case MSG_USER_CLOSE:
			intel_netlink_fini();
			use_pid = 0;
			break;
		default:
			break;
	}
out:
	return;
}

int  netlink_intel_send(int groupid,int grouptype,char * url,int len )
{
	return 0;
}

int intel_netlink_init(void)
{
	int ret = 0;
	intel_sock = netlink_kernel_create(&init_net, NETLINK_URL_INTEL,
			0, kernel_receive,NULL, THIS_MODULE);
	if(!intel_sock){
		ret = -1;
		goto out;
	}
	return ret;
}
void intel_netlink_fini(void)
{
	netlink_kernel_release(intel_sock );
}



