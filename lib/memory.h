#ifndef __MPSE_MEMORY_H__
#define __MPSE_MEMORY_H__
#include <stdlib.h>

#define MPSE_DEBUG(fmt, arg...) \
        do { \
                unsigned char mpse_debug_buf[128]; \
                memset(mpse_debug_buf, 0, 128); \
                sprintf(mpse_debug_buf, "echo \"(%s)<%d>:"fmt"\" >> /root/log", \
                        __func__, __LINE__, ##arg); \
                printf("%s\n", mpse_debug_buf); \
                system(mpse_debug_buf); \
        } while (0)

void * mem_alloc(unsigned int size);
void mem_free(void *p);
void mem_show();

#endif
