#ifndef __MPSE_H__
#define __MPSE_H__

enum mpse_method
{
	MPSE_AC,
	MPSE_ACF,
	MPSE_ACS,
	MPSE_ACB,
	MPSE_ACSB,
	MPSE_BNFA
};


void * mpse_new(char *name, int method, void (*priv_data_free)(void *p));
int mpse_add_pattern(void *obj, unsigned char *pat, int n, int nocase,
		int offset, int depth, void *priv_data, int id);
int mpse_pattern_count(void *obj);
int mpse_compile(void *obj);
int mpse_search(void *obj, unsigned char *tx, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state);
void mpse_free(void *obj);

#endif

