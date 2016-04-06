
#ifndef __INTELI_HASH__
#define __INTELI_HASH__

#include "../include/url.h"

extern int init_hash();
extern void release_hash();
extern int init_sock();
extern void release_sock();

extern void dumptable();
extern int findandinsert(struct url_item *);
extern int findanddeletet(struct url_item *);
extern int recv_msg(struct url_item *request);
//extern int send_msg(void *data,int len);
//extern int send_msg(int type,void *data);
extern int send_msg2(int type,void *data,int len);
#endif
