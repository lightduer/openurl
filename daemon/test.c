#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include "inteli_sock_hash.h"
#include "../include/url.h"

int main()
{
	struct iovec iov[4];
	struct url_item_result_kernel re;
	memset(&re,0,sizeof(struct url_item_result_kernel));
	re.type = 1;
	memcpy(re.host,"www.baidu.com",strlen("www.baidu.com"));
	
	iov[1].iov_base = &re;
	iov[1].iov_len = sizeof(re);
	re.type = 2;
	iov[2].iov_base = &re;
	iov[2].iov_len = sizeof(re);
	re.type = 2;
	iov[3].iov_base = &re;
	iov[3].iov_len = sizeof(re);

	/*
	struct url_item_result re;
	memset(&re,0,sizeof(struct url_item_result));
	re.user.type = 1;
	memcpy(re.user.host,"www.baidu.com",strlen("www.baidu.com"));*/
	init_sock();
	while(1){
	//	send_msg(MSG_ADD_URL,&re);		
		send_msg2(MSG_ADD_URL,iov,4);
		printf("send once!\n");
		sleep(1);
	}
	release_sock();
	return 0;
}
