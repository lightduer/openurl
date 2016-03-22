
#ifndef __BNFA_SEARCH_H__
#define __BNFA_SEARCH_H__

/*
*   DEFINES and Typedef's
*/
//#define SPARSE_FULL_STATE_0
#define BNFA_MAX_ALPHABET_SIZE          256
#define BNFA_FAIL_STATE                 0xffffffff
#define BNFA_SPARSE_LINEAR_SEARCH_LIMIT 6

#define BNFA_SPARSE_MAX_STATE           0x00ffffff
#define BNFA_SPARSE_COUNT_SHIFT         24
#define BNFA_SPARSE_VALUE_SHIFT         24

#define BNFA_SPARSE_MATCH_BIT           0x80000000
#define BNFA_SPARSE_FULL_BIT            0x40000000
#define BNFA_SPARSE_COUNT_BITS          0x3f000000
#define BNFA_SPARSE_MAX_ROW_TRANSITIONS 0x3f

typedef  unsigned int   bnfa_state_t;


/*
*   Internal Pattern Representation
*/
typedef struct bnfa_pattern
{
	struct bnfa_pattern * next;

	unsigned char       * patrn;   /* case specific */
	int                   n;           /* pattern len */
	int                   nocase;      /* nocase flag */
	int                   offset;
	int                   depth;
	int                   id;
	void                * private;    /* ptr to users pattern data/info  */
} bnfa_pattern_t;

/*
 *  List format transition node
 */
typedef struct bnfa_trans_node_s
{
	bnfa_state_t               key;
	bnfa_state_t               next_state;
	struct bnfa_trans_node_s * next;

} bnfa_trans_node_t;

/*
 *  List format patterns
 */
typedef struct bnfa_match_node_s
{
	void                     * data;
	struct bnfa_match_node_s * next;

} bnfa_match_node_t;

/*
 *  Final storage type for the state transitions
 */
enum {
	BNFA_FULL,
	BNFA_SPARSE
};

/*
 *   Aho-Corasick State Machine Struct
 */
typedef struct {
	int                method;
	int                format;
	int                alphabet_size;
	int                opt;

	unsigned int       pattern_cnt;
	bnfa_pattern_t     *patterns;

	int                max_states;
	int                num_states;
	int		   num_trans;
	int                match_states;

	bnfa_trans_node_t  **trans_table;

	bnfa_state_t       **next_state;
	bnfa_match_node_t  **match_list;
	bnfa_state_t       *fail_state;

	bnfa_state_t       *trans_list;
	int                force_full_zero_state;

	void               (*priv_data_free)(void *);

#define MAX_INQ 32
	unsigned inq;
	unsigned inq_flush;
	void * q[MAX_INQ];
}bnfa_struct_t;

/*
 *   Prototypes
 */
bnfa_struct_t * bnfa_new(void (*priv_data_free)(void *p));
void bnfa_set_opt(bnfa_struct_t  *p, int flag);
void bnfa_free(bnfa_struct_t *pstruct);

int bnfa_add_pattern(bnfa_struct_t *pstruct,
		unsigned char *pat, int patlen, int nocase,
		int offset, int depth,
		void *priv_data, int id);

int bnfa_compile(bnfa_struct_t *pstruct);

int bnfa_search(bnfa_struct_t *pstruct, unsigned char *t, int tlen,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state);

int bnfa_pattern_count(bnfa_struct_t *p);

#endif
