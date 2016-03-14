#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "../include/url.h"
#include "list.h"

#define MAX_HASH_SIZE 100

struct hashnode
{
	struct hlist_node hnode;
	struct url_item_request *data;
};

struct hashtable
{
	struct hlist_head head[MAX_HASH_SIZE];
	pthread_mutex_t lock;	
};

struct hashtable *table;

static int equal(struct url_item_request *data1,struct url_item_request *data2)
{
	if(strcmp(data1->host,data2->host) == 0 
			&& strcmp(data1->path,data2->path) == 0
			&&data1->method == data2->method
			&&data1->dst == data2->dst)
		return 1;
	else
		return 0;
}

static struct hashnode *lookup(struct url_item_request *data,int *r)
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
			free(node->data);
		}
	}
	pthread_mutex_unlock(&table->lock);
	pthread_mutex_destroy(&table->lock);
	free(table);
	return;
}

int findandinsert(struct url_item_request *data)
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

int findanddelete(struct url_item_request *data)
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
	free(node->data);
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
	struct url_item_request *data;
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
