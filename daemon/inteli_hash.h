
#ifndef __INTELI_HASH__
#define __INTELI_HASH__

#include "../include/url.h"

extern int init_hash();
extern void release_hash();

extern void dumptable();
extern int findandinsert(struct url_item_request *);
extern int findanddeletet(struct url_item_request *);

#endif
