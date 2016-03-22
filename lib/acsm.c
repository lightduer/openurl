/*
 * acsm code
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/netfilter.h>
#include <linux/types.h>

#include "memory.h"
#include "acsm.h"

#define AC_MALLOC(n) mem_alloc(n)
#define AC_FREE(p) \
	do { \
		if (p) { \
			mem_free(p); \
			p = NULL; \
		} \
	} while(0)


/*
 *    Simple QUEUE NODE
 */
typedef struct _qnode
{
	int state;
	struct _qnode *next;
} QNODE;

/*
 *    Simple QUEUE Structure
 */
typedef struct _queue
{
	QNODE *head, *tail;
	int count;
} QUEUE;

/*
 *
 */
static void queue_init(QUEUE *s)
{
	s->head = s->tail = 0;
	s->count = 0;
}

/*
 *  Add Tail Item to queue
 */
static void queue_add(QUEUE *s, int state)
{
	QNODE *q;

	if (!s->head)
	{
		q = s->tail = s->head = AC_MALLOC(sizeof(*q));
		if (q == NULL)
			return;

		q->state = state;
		q->next = 0;
	}
	else
	{
		q = AC_MALLOC(sizeof(*q));
		if (q == NULL)
			return;

		q->state = state;
		q->next = 0;
		s->tail->next = q;
		s->tail = q;
	}
	s->count++;
}


/*
 *  Remove Head Item from queue
 */
static int queue_remove(QUEUE * s)
{
	int state = 0;
	QNODE *q;

	if (s->head)
	{
		q = s->head;
		state = q->state;
		s->head = s->head->next;
		s->count--;
		if (!s->head)
		{
			s->tail = 0;
			s->count = 0;
		}
		AC_FREE(q);
	}
	return state;
}

static int queue_count(QUEUE * s)
{
	return s->count;
}

static void queue_free(QUEUE * s)
{
	while (queue_count(s))
	{
		queue_remove(s);
	}
}


/*
 ** Case Translation Table
 */
static int xlatcase_init = 0;
static unsigned char xlatcase[256];

/*
 *
 */
static void init_xlatcase(void)
{
	int i;

	if (xlatcase_init)
		return;

	for (i = 0; i < 256; i++)
	{
		xlatcase[i] = (unsigned char)toupper (i);
	}
	xlatcase_init = 1;
}

static inline void convert_case(unsigned char *d, unsigned char *s, int m)
{
	int i;
	for (i = 0; i < m; i++)
	{
		d[i] = xlatcase[s[i]];
	}
}

/*
 *
 */
static struct acsm_pattern * copy_match_list_entry(struct acsm_pattern *px)
{
	struct acsm_pattern *p;

	p = AC_MALLOC(sizeof(*p));
	if (p == NULL)
	{
		return NULL;
	}

	memcpy(p, px, sizeof(*p));
	p->next = 0;

	return p;
}

/*
 *  Add a pattern to the list of patterns terminated at this state.
 *  Insert at front of list.
 */
static void add_match_list_entry(struct acsm_struct *acsm, 
		int state, struct acsm_pattern *px)
{
	struct acsm_pattern *p;

	p = AC_MALLOC(sizeof(*p));
	if (p == NULL)
		return;

	memcpy(p, px, sizeof(*p));
	p->next = acsm->state_table[state].match_list;
	acsm->state_table[state].match_list = p;

	MPSE_DEBUG("add pattern:%s, state:%d", px->patrn, state);
}

/*
   Add Pattern States
 */
static void add_pattern_states(struct acsm_struct *acsm, struct acsm_pattern *p)
{
	unsigned char *pattern;
	unsigned char c;
	int state = 0;
	int next;
	int n;

	n = p->n;
	pattern = p->patrn;

	/*
	 *  Match up pattern with existing states
	 */
	for (; n > 0; pattern++, n--)
	{
		convert_case(&c, pattern, 1);
		next = acsm->state_table[state].next_state[c];
		if (next == ACSM_FAIL_STATE)
			break;
		state = next;
	}

	/*
	 *  Add new states for the rest of the pattern bytes, 1 state per byte
	 */
	for (; n > 0; pattern++, n--)
	{
		convert_case(&c, pattern, 1);
		acsm->num_states++;
		acsm->state_table[state].next_state[c] = acsm->num_states;
		state = acsm->num_states;
	}

	add_match_list_entry(acsm, state, p);
}

/*
 *   Build Deterministic Finite Automata
 */
static void build_dfa(struct acsm_struct *acsm)
{
	int r, s;
	int i;
	QUEUE q, *queue = &q;
	struct acsm_statetable *state_table = acsm->state_table;
	struct acsm_pattern *px;
	struct acsm_pattern *mlist;

	/* Init a Queue */
	queue_init(queue);

	/* Add the state 0 transitions 1st */
	for (i = 0; i < ALPHABET_SIZE; i++)
	{
		s = state_table[0].next_state[i];
		if (s)
		{
			queue_add(queue, s);
			state_table[s].fail_state = 0;
		}
	}

	/* Build the fail state transitions for each valid state */
	while (queue_count(queue) > 0)
	{
		r = queue_remove(queue);

		/* Find Final States for any Failure */
		for (i = 0; i < ALPHABET_SIZE; i++)
		{
			int fs, next;
			s = state_table[r].next_state[i];
			if (s != ACSM_FAIL_STATE)
			{
				queue_add(queue, s);
				fs = state_table[r].fail_state;

				/*
				 *  Locate the next valid state for 'i' 
				 *  starting at s
				 */

				next = state_table[fs].next_state[i];
				while (next == ACSM_FAIL_STATE)
				{
					next = state_table[fs].next_state[i];
					fs = state_table[fs].fail_state;
				}

				/*
				 *  Update 's' state failure state 
				 *  to point to the next valid state
				 */
				state_table[s].fail_state = next;

				/*
				 *  Copy 'next'states match_list to 's' states match_list,
				 *  we copy them so each list can be AC_FREE'd later,
				 *  else we could just manipulate pointers to fake the copy.
				 */
				for (mlist = state_table[next].match_list;
						mlist != NULL ;
						mlist = mlist->next)
				{
					px = copy_match_list_entry(mlist);
					if (px == NULL)
						continue;

					/* Insert at front of match_list */
					px->next = state_table[s].match_list;
					state_table[s].match_list = px;
				}
			}
			else
			{
				fs = state_table[r].fail_state;
				state_table[r].next_state[i] =
					state_table[fs].next_state[i];
			}
		}
	}

	/* Clean up the queue */
	queue_free(queue);
}

/*
 *
 */
struct acsm_struct * acsm_new(void (*priv_data_free)(void *p))
{
	struct acsm_struct * p;

	init_xlatcase();
	p = AC_MALLOC(sizeof(*p));
	if (p)
	{
		memset(p, 0, sizeof(*p));
		p->priv_data_free = priv_data_free;
	}

	return p;
}

/*
 *   Add a pattern to the list of patterns for this state machine
 */
int acsm_add_pattern(struct acsm_struct *p, unsigned char *pat, int n, int nocase,
		int offset, int depth, void *priv_data, int id)
{
	struct acsm_pattern *plist;

	if (p == NULL)
		return -1;

	plist = AC_MALLOC(sizeof(*plist));
	if (plist == NULL)
		return -1;
	plist->patrn = AC_MALLOC(n);
	if (plist->patrn == NULL)
	{
		AC_FREE(plist);
		return -1;
	}
	memcpy(plist->patrn, pat, n);

	plist->private = priv_data;
	plist->n = n;
	plist->nocase = nocase;
	plist->offset = offset;
	plist->depth = depth;
	plist->id = id;

	plist->next = p->patterns;
	p->patterns = plist;
	p->num_patterns++;

	return 0;
}

/*
 *   Compile State Machine
 */
int acsm_compile(struct acsm_struct *acsm)
{
	int i, k;
	struct acsm_pattern *plist;
	int size;

	if (acsm == NULL)
		return -1;

	/* Count number of states */
	acsm->max_states = 1;
	for (plist = acsm->patterns; plist != NULL; plist = plist->next)
	{
		acsm->max_states += plist->n;
	}
	size = sizeof(struct acsm_statetable) * acsm->max_states;
	acsm->state_table = AC_MALLOC(size);
	if (acsm->state_table == NULL)
	{
		acsm->max_states = 0;
		return -1;
	}
	memset(acsm->state_table, 0, size);

	/* Initialize state zero as a branch */
	acsm->num_states = 0;

	/* Initialize all States next_states to FAILED */
	for (k = 0; k < acsm->max_states; k++)
	{
		for (i = 0; i < ALPHABET_SIZE; i++)
		{
			acsm->state_table[k].next_state[i] = ACSM_FAIL_STATE;
		}
	}

	/* Add each Pattern to the State Table */
	for (plist = acsm->patterns; plist != NULL; plist = plist->next)
	{
		add_pattern_states(acsm, plist);
	}

	/* Set all failed state transitions to return to the 0'th state */
	for (i = 0; i < ALPHABET_SIZE; i++)
	{
		if (acsm->state_table[0].next_state[i] == ACSM_FAIL_STATE)
		{
			acsm->state_table[0].next_state[i] = 0;
		}
	}

	/* Build the DFA  */
	build_dfa(acsm);
	mem_show();
	return 0;
}

static int pattern_is_match_nocase(unsigned char *tx, int index, 
		unsigned char *pat, int len)
{
	if (index < 0)
		return 0;

	if (memcmp(tx + index, pat, len) == 0)
		return 1;

	return 0;
}

static int pattern_is_in_range(int index, int len, int offset, int depth)
{
	if (offset < 0 || depth < 0)
		return 0;

	if (offset && offset > index)
		return 0;

	if (depth && (offset + depth) < (index + len))
		return 0;

	return 1;
}

static int pattern_attr_is_match(unsigned char *tx, int index, 
		unsigned char *pat, int len,
		int nocase, int offset, int depth)
{
	if (nocase == 0)
	{
		if (pattern_is_match_nocase(tx, index, pat, len) == 0)
			return 0;
	}

	return pattern_is_in_range(index, len, offset, depth);
}

/*
 *   Search Text or Binary Data for Pattern matches
 */
int acsm_search(struct acsm_struct *acsm, unsigned char *tx, int n,
		int(*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state)
{
	int state = 0;
	struct acsm_pattern *mlist;
	unsigned char *tend;
	struct acsm_statetable *state_table = acsm->state_table;
	unsigned char *t;
	unsigned char tc;
	int index;

	if (acsm == NULL)
		return 0;

	if (!current_state)
	{
		return 0;
	}

	state = *current_state;
	if (state > acsm->num_states || state < 0)
                state = 0;

	t = tx;
	tend = t + n;
	for (; t < tend; t++)
	{
		convert_case(&tc, t, 1);
		state = state_table[state].next_state[tc];
		MPSE_DEBUG("process character %c, state: %d", tc, state);
		if (state_table[state].match_list != NULL)
		{
			mlist = state_table[state].match_list;

			while (mlist)
			{
				index = t - mlist->n + 1 - tx;

				if (pattern_attr_is_match(tx, index, 
							mlist->patrn,
							mlist->n, 
							mlist->nocase,
							mlist->offset, 
							mlist->depth) == 0)
				{
					mlist = mlist->next;
					continue;
				}

				if (match == NULL)
				{
					*current_state = state;
					return 1;
				}

				if (match(mlist->private, index, mlist->id, data) > 0)
				{
					*current_state = state;
					return 1;
				}
				mlist = mlist->next;
			}
		}
	}
	*current_state = state;
	return 0;
}


/*
 *   Free all memory
 */
void acsm_free(struct acsm_struct *acsm)
{
	int i;
	struct acsm_pattern *mlist, *ilist;

	if (acsm == NULL)
		return;

	for (i = 0; i < acsm->max_states; i++)
	{
		mlist = acsm->state_table[i].match_list;
		while (mlist)
		{
			ilist = mlist;
			mlist = mlist->next;
			AC_FREE(ilist);
		}
	}

	AC_FREE(acsm->state_table);

	mlist = acsm->patterns;
	while (mlist)
	{
		ilist = mlist;
		mlist = mlist->next;

		if (acsm->priv_data_free && ilist->private)
			acsm->priv_data_free(ilist->private);

		AC_FREE(ilist->patrn);
		AC_FREE(ilist);
	}
	AC_FREE(acsm);
}

int acsm_pattern_count(struct acsm_struct * acsm)
{
	return acsm->num_patterns;
}


