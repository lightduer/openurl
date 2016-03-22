#ifndef __MPSE_ACSM_H__
#define __MPSE_ACSM_H__

#define ALPHABET_SIZE    256

#define ACSM_FAIL_STATE   -1 

struct acsm_pattern
{

    struct acsm_pattern *next;

    unsigned char *patrn;
    int n;

    int nocase;
    int offset;
    int depth;

    int id;
    void *private;
};


struct acsm_statetable
{    
    /* Next state - based on input character */
    int next_state[ALPHABET_SIZE];  

    /* Failure state - used while building NFA & DFA  */
    int fail_state;   

    /* List of patterns that end here, if any */
    struct acsm_pattern *match_list;   
}; 


/*
* State machine Struct
*/
struct acsm_struct
{
    int max_states;  
    int num_states;  
    int num_patterns;

    struct acsm_pattern *patterns;
    struct acsm_statetable *state_table;

    void (*priv_data_free)(void *p);
};

/*
*   Prototypes
*/
struct acsm_struct * acsm_new(void (*priv_data_free)(void *p));
int acsm_add_pattern(struct acsm_struct *p, unsigned char *pat, int n, int nocase,
		int offset, int depth, void *priv_data, int id);
int acsm_pattern_count(struct acsm_struct * acsm);
int acsm_compile(struct acsm_struct *acsm);
int acsm_search(struct acsm_struct *acsm, unsigned char *tx, int n,
		int(*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state);
void acsm_free(struct acsm_struct *acsm);

#endif
