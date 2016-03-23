#ifndef __MPSE_MEMORY_H__
#define __MPSE_MEMORY_H__
#include <stdlib.h>

#define MPSE_DEBUG(fmt, arg...) \
        do {\
        } while (0)

void * mem_alloc(unsigned int size);
void mem_free(void *p);
void mem_show();

#endif
