/*
 * Multi-Pattern Search Engine
 */

#include <stdio.h>
#include <string.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/netfilter.h>
#include <linux/types.h>

#include "memory.h"
#include "acsm.h"
#include "acsm2.h"
#include "bnfa.h"
#include "mpse.h"

/* local struct */
struct mpse_struct
{
	char name[16];
	__u16 method;
	__u16 flags;
	void *object;
};

/*
 * functions
 */
void * mpse_new(char *name, int method, void (*priv_data_free)(void *p))
{
	struct mpse_struct *p;

	p = (struct mpse_struct *)mem_alloc(sizeof(*p));
	if (p == NULL)
		return NULL;

	switch (method)
	{
	case MPSE_AC:
		p->object = acsm_new(priv_data_free);
		break;
	case MPSE_ACF:
		p->object = acsm_new2(priv_data_free);
		if (p->object)
		{
			acsm_select_format2(p->object, ACF_FULL);
		}
		break;
	case MPSE_ACS:
		p->object = acsm_new2(priv_data_free);
		if (p->object)
		{
			acsm_select_format2(p->object, ACF_SPARSE);
		}
		break;
	case MPSE_ACB:
		p->object = acsm_new2(priv_data_free);
		if (p->object)
		{
			acsm_select_format2(p->object, ACF_BANDED);
		}
		break;
	case MPSE_ACSB:
		p->object = acsm_new2(priv_data_free);
		if (p->object)
		{
			acsm_select_format2(p->object, ACF_SPARSEBANDS);
		}
		break;
	case MPSE_BNFA:
		p->object = bnfa_new(priv_data_free);
		break;
	default:
		p->object = NULL;
		break;
	}

	if (p->object)
	{
		p->method = method;
		strncpy(p->name, name, sizeof(p->name));
	}
	else
	{
		mem_free(p);
		p = NULL;
	}

	return p;
}

int mpse_add_pattern(void *obj, unsigned char *pat, int n, int nocase,
		int offset, int depth, void *priv_data, int id)
{
	struct mpse_struct *mpse = obj;
	int ret;

	if  (mpse == NULL)
		return -1;

	switch (mpse->method)
	{
	case MPSE_AC:
		ret = acsm_add_pattern(mpse->object, pat, n, nocase, 
				offset, depth, priv_data, id);
		break;
	case MPSE_ACF:
	case MPSE_ACS:
	case MPSE_ACB:
	case MPSE_ACSB:
		ret = acsm_add_pattern2(mpse->object, pat, n, nocase, 
				offset, depth, priv_data, id); 
		break;
	case MPSE_BNFA:
		ret = bnfa_add_pattern(mpse->object, pat, n, nocase, 
				offset, depth, priv_data, id); 
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

int mpse_pattern_count(void *obj)
{
	struct mpse_struct *mpse = obj;
	int ret;

	if  (mpse == NULL)
		return 0;

	switch (mpse->method)
	{
	case MPSE_AC:
		ret = acsm_pattern_count(mpse->object); 
		break;
	case MPSE_ACF:
	case MPSE_ACS:
	case MPSE_ACB:
	case MPSE_ACSB:
		ret = acsm_pattern_count2(mpse->object); 
		break;
	case MPSE_BNFA:
		ret = bnfa_pattern_count(mpse->object); 
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

int mpse_compile(void *obj)
{
	struct mpse_struct *mpse = obj;
	int ret;

	if  (mpse == NULL)
		return -1;

	switch (mpse->method)
	{
	case MPSE_AC:
		ret = acsm_compile(mpse->object); 
		break;
	case MPSE_ACF:
	case MPSE_ACS:
	case MPSE_ACB:
	case MPSE_ACSB:
		ret = acsm_compile2(mpse->object); 
		break;
	case MPSE_BNFA:
		ret = bnfa_compile(mpse->object); 
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

/*   > 0:  found
 *  == 0: not found
 */
int mpse_search(void *obj, unsigned char *tx, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state)
{
	struct mpse_struct *mpse = obj;
	int ret;

	if  (mpse == NULL)
		return 0;

	switch (mpse->method)
	{
	case MPSE_AC:
		ret = acsm_search(mpse->object, tx, n, match, data, current_state); 
		break;
	case MPSE_ACF:
	case MPSE_ACS:
	case MPSE_ACB:
	case MPSE_ACSB:
		ret = acsm_search2(mpse->object, tx, n, match, data, current_state); 
		break;
	case MPSE_BNFA:
		ret = bnfa_search(mpse->object, tx, n, match, data, current_state); 
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

void mpse_free(void *obj) 
{
	struct mpse_struct *mpse = obj;

	if  (mpse == NULL)
		return;

	switch (mpse->method)
	{
	case MPSE_AC:
		acsm_free(mpse->object); 
		break;
	case MPSE_ACF:
	case MPSE_ACS:
	case MPSE_ACB:
	case MPSE_ACSB:
		acsm_free2(mpse->object); 
		break;
	case MPSE_BNFA:
		bnfa_free(mpse->object);
		break;
	default:
		break;
	}
	
	mem_free(mpse);
	return;
}

