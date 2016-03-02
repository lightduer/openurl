
#ifndef __STUDY_URL__
#define __STUDY_URL__

#define MSG_USER_PID 1
#define MSG_USER_CLOSE 2
#define MSG_ADD_URL 3
#define MSG_DEL_URL 4

#define NETLINK_URL_INTEL 24
#define NETLINK_URL_COMMTOUCH 25

#define URL_HOST_LEN 256
#define URL_PATH_LEN 2048

struct url_item_request
{
	char method[6];
	char dst[16];
	char host[URL_HOST_LEN];
	char path[URL_PATH_LEN];
	struct nlmsghdr hdr;
};

struct url_item_result
{
	unsigned short type;
	char host[URL_HOST_LEN];
	char path[URL_PATH_LEN];
	struct nlmsghdr hdr; 
};

#endif
