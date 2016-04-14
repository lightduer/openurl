#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <sys/uio.h>
#include "list.h"
#include "../include/url.h"

#define MAX_HASH_SIZE 100

struct hashnode
{
	struct hlist_node hnode;
	struct url_item *data;
};

struct hashtable
{
	struct hlist_head head[MAX_HASH_SIZE];
	pthread_mutex_t lock;	
};

struct hashtable *table;

static int equal(struct url_item *data1,struct url_item *data2)
{
	if(strcmp(data1->host,data2->host) == 0 
			&& strcmp(data1->path,data2->path) == 0
			&&data1->method == data2->method
			&&data1->dst == data2->dst)
		return 1;
	else
		return 0;
}

static struct hashnode *lookup(struct url_item *data,int *r)
{
	int i;
	struct hashnode *node = NULL;
	struct hlist_node *n;
	for(i = 0;i < MAX_HASH_SIZE;i++){
		hlist_for_each_entry(node,n,&table->head[i],hnode){
			if(1 == equal(data,node->data)){
				if(r != NULL)
					*r = i;
				goto out;
			}
		}
	}
out:	
	return n ? node : NULL;
}



static void insert(int i,struct hashnode *node)
{
	hlist_add_head(&node->hnode,&table->head[i]);
}
static void delete(struct hashnode *node)
{
	hlist_del(&node->hnode);	
}

int init_hash()
{
	int i;
	int ret = 0;
	table = (struct hashtable *)malloc(sizeof(struct hashtable));
	if(table == NULL){
		printf("table malloc error!\n");
		ret = -1;
		goto out;
	}
	pthread_mutex_init(&table->lock,NULL);
	for(i = 0;i < MAX_HASH_SIZE;i++){
		INIT_HLIST_HEAD(&table->head[i]);
	}
out:
	return ret;
}
void release_hash()
{
	int i;
	struct hashnode *node;
	struct hlist_node *n, *next;
	
	pthread_mutex_lock(&table->lock);
	for(i = 0;i < MAX_HASH_SIZE;i++){
	hlist_for_each_entry_safe(node,n,next,&table->head[i],hnode){
			delete(node);
			free(node);
			//free(node->data);
		}
	}
	pthread_mutex_unlock(&table->lock);
	pthread_mutex_destroy(&table->lock);
	free(table);
	return;
}

int findandinsert(struct url_item *data)
{
	int i = 0;
	int ret = 0;
	struct hashnode *node = NULL;
	pthread_mutex_lock(&table->lock);
	node = lookup(data,&i);
	if(node != NULL){
		ret = -1;
		goto out;
	}
	node = (struct hashnode *)malloc(sizeof(struct hashnode));
	if(node == NULL){
		ret = -1;
		goto out;
	}
	INIT_HLIST_NODE(&node->hnode);
	node->data = data;
	insert(i,node);
out:
	pthread_mutex_unlock(&table->lock);
	return ret;
}

int findanddelete(struct url_item *data)
{
	int ret = 0;
	struct hashnode *node = NULL;
	pthread_mutex_lock(&table->lock);
	node = lookup(data,NULL);
	if(node == NULL){
		ret = -1;
		goto out;
	}
	delete(node);
	//free(node->data);
	free(node);
out:
	pthread_mutex_unlock(&table->lock);
	return ret;
}

void dumptable()
{
	int i;
	struct hashnode *node = NULL;
	struct hlist_node *n;
	struct url_item *data;
	printf("dump start==========>\n");
	pthread_mutex_lock(&table->lock);
	for(i = 0;i < MAX_HASH_SIZE;i++){
		hlist_for_each_entry(node,n,&table->head[i],hnode){
			data = node->data;

			printf("url:%s%s\n",data->host,data->path);
		}
	}
	pthread_mutex_unlock(&table->lock);
	printf("dump end\n");

}

#if 0
#include <stdio.h>
#include <string.h>
int main()
{
	int ret = -1;
	init_hash();
	struct url_item_request *data1 = (struct url_item_request *) malloc(sizeof(struct url_item_request));
	if(data1 == NULL)
		goto out;
	data1->method = 0;
	data1->dst = 0;
	strcpy(data1->host,"www.baidu.com");
	strcpy(data1->path,"/1.html");
	
	struct url_item_request *data2 = (struct url_item_request *) malloc(sizeof(struct url_item_request));
	if(data2 == NULL)
		goto out;
	data2->method = 0;
	data2->dst = 0;
	//data2->dst = 1;
	strcpy(data2->host,"www.baidu.com");
	strcpy(data2->path,"/1.html");

	ret = findandinsert(data1);
	printf("ret is %d\n",ret);
//	ret = findanddelete(data1);
//	printf("ret is %d\n",ret);
	ret = findandinsert(data2);
	printf("ret is %d\n",ret);
	ret = findanddelete(data2);
	printf("ret is %d\n",ret);
out:
	release_hash();
	//ret = findandinsert(data1);

	return 0;
}
#endif

int netlink_sock;

int send_msg2(int type,void *data,int len)
{
	int i,ret = -1;
	struct iovec *iov = (struct iovec *)data;
	struct msghdr msg;
	struct nlmsghdr hdr;
	struct sockaddr_nl kpeer;

	memset(&msg,0,sizeof(struct msghdr));
	memset(&hdr, 0, sizeof(hdr));                                                                              
	
	hdr.nlmsg_flags = 0;
	hdr.nlmsg_type = type;
	hdr.nlmsg_pid = getpid();                                                                
	hdr.nlmsg_len = NLMSG_LENGTH(0);
	
	kpeer.nl_family = AF_NETLINK;                                                                             
	kpeer.nl_pid = 0; 
	kpeer.nl_groups = 0;
	
	iov[0].iov_base = (void *)&hdr;
	iov[0].iov_len = sizeof(struct nlmsghdr);
	
	msg.msg_name = (void*)&kpeer;
	msg.msg_namelen = sizeof(struct sockaddr_nl);

	msg.msg_iov = iov;
	msg.msg_iovlen = len;
	sendmsg(netlink_sock,&msg,0);
}
/*
int send_msg(int type ,void *data)
{
	int ret = -1;
	struct sockaddr_nl kpeer;                                                        
	socklen_t kpeer_len;
	struct nlmsghdr msghdr;
	struct url_item_hdr result_msg; 
	
	if(data)
		result_msg->data=(struct url_item *)data;                                                                                           
	
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
				msghdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct url_item));
				memcpy(&result_msg->hdr,&msghdr,sizeof(struct nlmsghdr));
				ret=sendto(netlink_sock, result_msg, msghdr.nlmsg_len,0,(struct sockaddr *)&kpeer, kpeer_len);
			}
			break;
		case MSG_USER_PID:
			msghdr.nlmsg_len = NLMSG_LENGTH(0);
			ret=sendto(netlink_sock, &msghdr, msghdr.nlmsg_len,
					0, (struct sockaddr *)&kpeer, kpeer_len);                      
			break;                                                                                            
		default:
			break;                                                                                                            
	}
	return ret;
}
*/
int recv_msg(struct url_item *request)
{
	struct sockaddr_nl kpeer;
	int rcvlen=0,kpeer_len=0;
	struct url_item_hdr info;	
	if(request == NULL){
		rcvlen = -1;
		goto out;
	}
	memset(&kpeer, 0, sizeof(kpeer));
	kpeer.nl_family = AF_NETLINK;
	kpeer.nl_pid = 0;
	kpeer.nl_groups = 0;
	kpeer_len = sizeof(kpeer);
	rcvlen = recvfrom(netlink_sock, &info, sizeof(struct url_item_hdr), 0, (struct sockaddr*)&kpeer, (socklen_t *)&kpeer_len);
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
	struct iovec iov[1];
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
	send_msg2(MSG_USER_PID,&iov,1);
out:
	return ret;
}

void release_sock()
{
	struct iovec iov[1];
	send_msg2(MSG_USER_CLOSE,&iov,1);
	if(netlink_sock > 0)
		close(netlink_sock);
	return;
}
