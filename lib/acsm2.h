#ifndef __MPSE_ACSM2_H__
#define __MPSE_ACSM2_H__


/*
*   DEFINES and Typedef's
*/
#define MAX_ALPHABET_SIZE 256     

typedef unsigned int acstate_t;
#define ACSM_FAIL_STATE2  0xffffffff

/*
 *
 */
typedef struct _acsm_pattern2
{      
	struct _acsm_pattern2 *next;

	unsigned char *patrn;
	int n;
	int nocase;
	int offset;
	int depth;
	void *private;
	int id;
} ACSM_PATTERN2;

typedef struct trans_node_s
{
	acstate_t key;           
	acstate_t next_state;
	struct trans_node_s *next;
} trans_node_t;


enum
{
	ACF_FULL,
	ACF_SPARSE,
	ACF_BANDED,
	ACF_SPARSEBANDS,
	ACF_FULLQ
};

enum
{
	FSA_TRIE,
	FSA_NFA,
	FSA_DFA
};

/*
 *   Aho-Corasick State Machine Struct - one per group of pattterns
 */
typedef struct
{

	int max_states;  
	int num_states;  

	ACSM_PATTERN2 *patterns;
	acstate_t     *fail_state;
	ACSM_PATTERN2 **match_list;

	/* list of transitions in each state, this is used to build the nfa & dfa */
	/* after construction we convert to sparse or full format matrix and free */
	/* the transition lists */
	trans_node_t **trans_table;

	acstate_t **next_state;
	int format;
	int sparse_max_row_nodes;
	int sparse_max_zcnt;

	int num_trans;
	int alphabet_size;
	int fsa;
	int num_patterns;
	void (*priv_data_free)(void *p);

	int sizeofstate;
	int compress_states;

} ACSM_STRUCT2;

/*
 *   Prototypes
 */
ACSM_STRUCT2 * acsm_new2(void (*priv_data_free)(void *p));
int acsm_add_pattern2(ACSM_STRUCT2 *p, unsigned char *pat, int n, int nocase,
		int offset, int depth, void *priv_data, int id);
int acsm_compile2(ACSM_STRUCT2 *acsm);
int acsm_search2(ACSM_STRUCT2 *acsm, unsigned char *text, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state);
void acsm_free2(ACSM_STRUCT2 *acsm);
int acsm_pattern_count2(ACSM_STRUCT2 * acsm);
int acsm_select_format2(ACSM_STRUCT2 *acsm, int m);

#endif
