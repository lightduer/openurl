#include <stdio.h>
#include <string.h>

#include "memory.h"

#if 0

/* mem unit object */
struct mem_object
{
	unsigned int size;
	int vmalloced;
	char object[0];
};

#define VMALLOC_SIZE  (96 * 1024)  /* over 96K use vmalloc */

static atomic_t *memory_use = NULL;

static inline void * __mem_alloc(unsigned int size, gfp_t gfp)
{
	struct mem_object *mo;
	unsigned int real_size = size + sizeof(struct mem_object);

	if (size == 0)
		return NULL;

	mo = kmalloc(real_size, gfp);
	if (mo == NULL)
		return NULL;

	mo->size = real_size;
	mo->vmalloced = 0;

	if (memory_use)
		atomic_add(real_size, memory_use);

	return (void *)mo->object;
}

static inline void * __mem_valloc(unsigned int size)
{
	struct mem_object *mo;
	unsigned int real_size = size + sizeof(*mo);

	if (size == 0)
		return NULL;

	if (size > 500 * 1024 *1024)
		return NULL;

	mo = vmalloc(real_size);
	if (mo == NULL)
		return NULL;

	mo->size = real_size;
	mo->vmalloced = 1;

	if (memory_use)
		atomic_add(real_size, memory_use);

	return (void *)mo->object;

}
#endif

static unsigned int memory_use = 0;

void * mem_alloc(unsigned int size)
{
	void *p = NULL;

	p = malloc(size);
	if (!p)
		return NULL;

	memory_use += size;

	return p;
}

#if 0
void * mem_zalloc(unsigned int size)
{
	void *p;

	if ((size + sizeof(struct mem_object)) > VMALLOC_SIZE)
		p = __mem_valloc(size);
	else
		p = __mem_alloc(size, GFP_KERNEL);

	if (p)
		memset(p, 0, size);

	return p;
}

void * mem_alloc_atomic(unsigned int size)
{
	return __mem_alloc(size, GFP_ATOMIC);
}

void * mem_zalloc_atomic(unsigned int size)
{
	void * p = __mem_alloc(size, GFP_ATOMIC);
	if (p)
		memset(p, 0, size);

	return p;
}

#endif

void mem_free(void *p)
{
	if (p == NULL)
		return;

	free(p);
	memory_use -= sizeof(*p);

	return;
}

void mem_show()
{
	MPSE_DEBUG("now memory use:%u", memory_use);
	return ;
}
