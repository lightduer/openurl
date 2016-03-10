#ifndef __INTEL_NETLINK__
#define __INTEL_NETLINK__

#include "../include/url.h"
extern void netlink_intel_send(char *url,int urllen,char *uri,int urilen,unsigned int ipaddr,int httpmethod); 
extern int intel_netlink_init(void);
extern void intel_netlink_fini(void);
#endif
