
#ifndef __STUDY_URL__
#define __STUDY_URL__

#include <linux/netlink.h>

#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_CONNECT 3

#define MSG_USER_PID 1
#define MSG_USER_CLOSE 2
#define MSG_ADD_URL 3
#define MSG_DEL_URL 4
#define MSG_STUDY_URL 5

#define NETLINK_URL_INTEL 24
#define NETLINK_URL_COMMTOUCH 25

#define URL_HOST_LEN 256
#define URL_PATH_LEN 2048

#define URL_INVALID_TYPE 0x0000

struct url_item
{
	char method;
	unsigned int dst;
	unsigned short type;
	char host[URL_HOST_LEN];
	char path[URL_PATH_LEN];
};

struct url_item_hdr
{
	struct nlmsghdr hdr;
	struct url_item data;
};

#endif
