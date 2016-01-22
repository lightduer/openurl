#ifndef __URL_SHARE__
#define __URL_SHARE__

#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_CONNECT 3

#define URL_MAX_LEN 256 
#define URI_MAX_LEN 2048

#define NETLINK_URL_INTEL 24
#define MSG_USER_PID    1
#define MSG_URL_RESULT  2
#define MSG_USER_CLOSE  3

struct intel_data
{
	char url[URL_MAX_LEN + 1];
	char *uri[URI_MAX_LEN + 1];
	unsigned int ipaddr;
	int httpmethod;
};

#endif
