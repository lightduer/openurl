/*
 * nfa code
 * use AC pattern
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
#include "bnfa.h"

/*
 * Used to initialize last state, states are limited to 0-16M
 * so this will not conflict.
 */
#define LAST_STATE_INIT  0xffffffff

/*
 * Custom memory allocator
 */
#define BNFA_MALLOC(n) mem_alloc(n)
#define BNFA_FREE(p) \
	do { \
		if (p) { \
			mem_free(p); \
			p = NULL; \
		} \
	} while (0)


/*
* Case Translation Table - his guarantees we use
* indexed lookups for case conversion
*/
static unsigned char xlatcase[BNFA_MAX_ALPHABET_SIZE];

static void init_xlatcase(void)
{
	int i;
	static int first=1;

	if (!first)
		return;

	for (i = 0; i < BNFA_MAX_ALPHABET_SIZE; i++)
	{
		xlatcase[i] = (unsigned char)toupper(i);
	}

	first = 0;
}

/*
 *    simple queue node
 */
typedef struct _qnode
{
	unsigned state;
	struct _qnode *next;
} QNODE;
/*
 *    simple fifo queue structure
 */
typedef struct _queue
{
	QNODE * head, *tail;
	int count;
	int maxcnt;
} QUEUE;
/*
 *   Initialize the fifo queue
 */
static void queue_init(QUEUE *s)
{
	s->head = s->tail = 0;
	s->count = 0;
	s->maxcnt = 0;
}
/*
 *  Add items to tail of queue (fifo)
 */
static int queue_add(QUEUE *s, int state)
{
	QNODE *q;

	if (!s->head)
	{
		q = s->tail = s->head = (QNODE *)BNFA_MALLOC(sizeof(QNODE));
		if (!q) 
			return -1;
		q->state = state;
		q->next = 0;
	}
	else
	{
		q = (QNODE *)BNFA_MALLOC(sizeof(QNODE));
		q->state = state;
		q->next = 0;
		s->tail->next = q;
		s->tail = q;
	}

	s->count++;

	if (s->count > s->maxcnt)
		s->maxcnt = s->count;

	return 0;
}

/*
 *  Remove items from head of queue (fifo)
 */
static int queue_remove(QUEUE *s)
{
	int state = 0;
	QNODE * q;

	if (s->head)
	{
		q       = s->head;
		state   = q->state;
		s->head = s->head->next;
		s->count--;

		if (!s->head)
		{
			s->tail = 0;
			s->count = 0;
		}
		BNFA_FREE(q);
	}

	return state;
}

/*
 *   Return count of items in the queue
 */
static int queue_count(QUEUE *s)
{
	return s->count;
}

/*
 *  Free the queue
 */
static void queue_free(QUEUE *s)
{
	while (queue_count(s))
	{
		queue_remove(s);
	}
}

/*
 *  Get next state from transition list
 */
static int bnfa_list_get_next_state(bnfa_struct_t *bnfa, int state, int input)
{
	if (state == 0) /* Full set of states  always */
	{
		bnfa_state_t *p = (bnfa_state_t *)bnfa->trans_table[0];
		if (!p)
		{
			return 0;
		}
		return p[input];
	}
	else
	{
		bnfa_trans_node_t *t = bnfa->trans_table[state];
		while (t)
		{
			if (t->key == (unsigned)input)
			{
				return t->next_state;
			}
			t = t->next;
		}
		return BNFA_FAIL_STATE; /* Fail state */
	}
}

/*
 *  Put next state - head insertion, and transition updates
 */
static int bnfa_list_put_next_state(bnfa_struct_t *bnfa, int state, int input, int next_state)
{
	if (state >= bnfa->max_states)
	{
		return -1;
	}

	if (input >= bnfa->alphabet_size)
	{
		return -1;
	}

	if (state == 0)
	{
		bnfa_state_t * p;

		p = (bnfa_state_t *)bnfa->trans_table[0];
		if (!p)
		{
			p = BNFA_MALLOC(sizeof(bnfa_state_t)*bnfa->alphabet_size);
			if (!p)
			{
				return -1;
			}

			bnfa->trans_table[0] = (bnfa_trans_node_t*)p;
		}
		if (p[input])
		{
			p[input] = next_state;
			return 0;
		}
		p[input] = next_state;
	}
	else
	{
		bnfa_trans_node_t *p;
		bnfa_trans_node_t *tnew;

		/* Check if the transition already exists, if so just update the next_state */
		p = bnfa->trans_table[state];
		while (p)
		{
			if (p->key == (unsigned)input)  /* transition already exists- reset the next state */
			{
				p->next_state = next_state;
				return 0;
			}
			p = p->next;
		}

		/* Definitely not an existing transition - add it */
		tnew = BNFA_MALLOC(sizeof(bnfa_trans_node_t));
		if (!tnew)
		{
			return -1;
		}

		tnew->key        = input;
		tnew->next_state = next_state;
		tnew->next       = bnfa->trans_table[state];

		bnfa->trans_table[state] = tnew;
	}

	bnfa->num_trans++;

	return 0;
}

/*
 *   Free the entire transition list table
 */
static int bnfa_list_free_table(bnfa_struct_t *bnfa)
{
	int i;
	bnfa_trans_node_t *t, *p;

	if (!bnfa->trans_table) 
		return 0;

	if (bnfa->trans_table[0])
	{
		BNFA_FREE(bnfa->trans_table[0]);
	}

	for (i = 1; i < bnfa->max_states; i++)
	{
		t = bnfa->trans_table[i];

		while (t)
		{
			p = t;
			t = t->next;
			BNFA_FREE(p);
		}
	}

	if (bnfa->trans_table)
	{
		BNFA_FREE(bnfa->trans_table);
		bnfa->trans_table = NULL;
	}

	return 0;
}


/*
 * Converts a single row of states from list format to a full format
 */
static int bnfa_list_conv_row_to_full(bnfa_struct_t * bnfa, 
		bnfa_state_t state, bnfa_state_t * full)
{
	if ((int)state >= bnfa->max_states) /* protects 'full' against overflow */
	{
		return -1;
	}

	if (state == 0)
	{
		if (bnfa->trans_table[0])
			memcpy(full, bnfa->trans_table[0],
					sizeof(bnfa_state_t)*bnfa->alphabet_size);
		else
			memset(full, 0, sizeof(bnfa_state_t)*bnfa->alphabet_size);

		return bnfa->alphabet_size;
	}
	else
	{
		int tcnt = 0;

		bnfa_trans_node_t *t = bnfa->trans_table[state];

		memset(full, 0, sizeof(bnfa_state_t) * bnfa->alphabet_size);

		if (!t)
		{
			return 0;
		}

		while (t && (t->key < BNFA_MAX_ALPHABET_SIZE))
		{
			full[t->key] = t->next_state;
			tcnt++;
			t = t->next;
		}
		return tcnt;
	}
}

/*
 *  Add pattern characters to the initial upper case trie
 *  unless Exact has been specified, in  which case all patterns
 *  are assumed to be case specific.
 */
static int bnfa_add_pattern_states(bnfa_struct_t *bnfa, bnfa_pattern_t *p)
{
	int            state, next, n;
	unsigned char *pattern;
	bnfa_match_node_t  *pmn;

	n       = p->n;
	pattern = p->patrn;
	state   = 0;

	/*
	 *  Match up pattern with existing states
	 */
	for (; n > 0; pattern++, n--)
	{
		next = bnfa_list_get_next_state(bnfa,state,xlatcase[*pattern]);
		if (next == (int)BNFA_FAIL_STATE || next == 0)
		{
			break;
		}
		state = next;
	}

	/*
	 *   Add new states for the rest of the pattern bytes, 1 state per byte, uppercase
	 */
	for (; n > 0; pattern++, n--)
	{
		bnfa->num_states++;

		if (bnfa_list_put_next_state(bnfa,state,xlatcase[*pattern],bnfa->num_states)  < 0)
			return -1;

		state = bnfa->num_states;

		if (bnfa->num_states >= bnfa->max_states)
		{
			return -1;
		}
	}

	/*  Add a pattern to the list of patterns terminated at this state */
	pmn = BNFA_MALLOC(sizeof(bnfa_match_node_t));
	if (!pmn)
	{
		return -1;
	}

	pmn->data = p;
	pmn->next = bnfa->match_list[state];

	bnfa->match_list[state] = pmn;

	return 0;
}

static int bnfa_conv_node_to_full(bnfa_trans_node_t *t, bnfa_state_t * full)
{
	int tcnt = 0;

	memset(full, 0, sizeof(bnfa_state_t)*BNFA_MAX_ALPHABET_SIZE);

	if (!t)
	{
		return 0;
	}

	while (t && (t->key < BNFA_MAX_ALPHABET_SIZE))
	{
		full[ t->key ] = t->next_state;
		tcnt++;
		t = t->next;
	}
	return tcnt;
}

static int KcontainsJ(bnfa_trans_node_t * tk, bnfa_trans_node_t *tj )
{
	bnfa_state_t  full[BNFA_MAX_ALPHABET_SIZE];

	if (!bnfa_conv_node_to_full(tk,full))
		return 1; /* emtpy state */

	while (tj)
	{
		if (!full[tj->key])
			return 0;

		tj = tj->next; /* get next tj key */
	}

	return 1;
}
/*
 * 1st optimization - eliminate duplicate fail states
 *
 * check if a fail state is a subset of the current state,
 * if so recurse to the next fail state, and so on.
 */
static int bnfa_opt_nfa(bnfa_struct_t *bnfa)
{
    int            cnt=0;
    int            k, fs, fr;
    bnfa_state_t * fail_state = bnfa->fail_state;

    for (k = 2; k < bnfa->num_states; k++)
    {
        fr = fs = fail_state[k];
        while (fs && KcontainsJ(bnfa->trans_table[k], bnfa->trans_table[fs]))
        {
            fs = fail_state[fs];
        }
        if (fr != fs)
        {
           cnt++;
           fail_state[k] = fs;
        }
    }

    return 0;
}

/*
 *   Build a non-deterministic finite automata using Aho-Corasick construction
 *   The keyword trie must already be built via bnfa_add_pattern_states()
 */
static int bnfa_build_nfa(bnfa_struct_t * bnfa)
{
	int             r, s, i;
	QUEUE           q, *queue = &q;
	bnfa_state_t     *fail_state = bnfa->fail_state;
	bnfa_match_node_t **match_list = bnfa->match_list;
	bnfa_match_node_t  *mlist;
	bnfa_match_node_t  *px;

	/* Init a Queue */
	queue_init(queue);

	/* Add the state 0 transitions 1st,
	 * the states at depth 1, fail to state 0
	 */
	for (i = 0; i < bnfa->alphabet_size; i++)
	{
		/* note that state zero deos not fail,
		 *  it just returns 0..nstates-1
		 */
		s = bnfa_list_get_next_state(bnfa, 0, i);
		if (s) /* don't bother adding state zero */
		{
			if (queue_add(queue, s))
			{
				queue_free(queue);
				return -1;
			}
			fail_state[s] = 0;
		}
	}

	/* Build the fail state successive layer of transitions */
	while (queue_count(queue) > 0)
	{
		r = queue_remove(queue);

		/* Find Final States for any Failure */
		for (i = 0; i < bnfa->alphabet_size; i++)
		{
			int fs, next;

			s = bnfa_list_get_next_state(bnfa,r,i);

			if (s == (int)BNFA_FAIL_STATE)
				continue;

			if (queue_add(queue, s))
			{
				queue_free(queue);
				return -1;
			}

			fs = fail_state[r];

			/*
			 *  Locate the next valid state for 'i' starting at fs
			 */
			while ((next=bnfa_list_get_next_state(bnfa,fs,i)) == (int)BNFA_FAIL_STATE)
			{
				fs = fail_state[fs];
			}

			/*
			 *  Update 's' state failure state to point to the next valid state
			 */
			fail_state[s] = next;

			/*
			 *  Copy 'next'states match_list into 's' states match_list,
			 *  we just create a new list nodes, the patterns are not copied.
			 */
			for (mlist = match_list[next];mlist;mlist = mlist->next)
			{
				/* Dup the node, don't copy the data */
				px = BNFA_MALLOC(sizeof(bnfa_match_node_t));
				if (!px)
				{
					queue_free(queue);
					return 0;
				}

				px->data = mlist->data;

				px->next = match_list[s]; /* insert at head */

				match_list[s] = px;
			}
		}
	}

	/* Clean up the queue */
	queue_free(queue);

	/* optimize the failure states */
	if (bnfa->opt)
		bnfa_opt_nfa(bnfa);

	return 0;
}

/*
 *  Conver state machine to full format
 */
static int bnfa_conv_list_to_full(bnfa_struct_t * bnfa)
{
	int          k;
	bnfa_state_t  *p;
	bnfa_state_t **next_state = bnfa->next_state;

	for (k = 0; k < bnfa->num_states; k++)
	{
		p = BNFA_MALLOC(sizeof(bnfa_state_t) * bnfa->alphabet_size);
		if (!p)
		{
			return -1;
		}
		bnfa_list_conv_row_to_full(bnfa, (bnfa_state_t)k, p);

		next_state[k] = p; /* now we have a full format row vector */
	}

	return 0;
}

/*
 *  Convert state machine to csparse format
 *
 *  Merges state/transition/failure arrays into one.
 *
 *  For each state we use a state-word followed by the transition list for
 *  the state sw(state 0)...tl(state 0) sw(state 1)...tl(state1) sw(state2)...
 *  tl(state2) ....
 *
 *  The transition and failure states are replaced with the start index of
 *  transition state, this eliminates the next_state[] lookup....
 *
 *  The compaction of multiple arays into a single array reduces the total
 *  number of states that can be handled since the max index is 2^24-1,
 *  whereas without compaction we had 2^24-1 states.
 */
static int bnfa_conv_list_to_csparse_array(bnfa_struct_t * bnfa)
{
	int              m, k, i, nc;
	bnfa_state_t     state;
	bnfa_state_t    *fail_state = (bnfa_state_t *)bnfa->fail_state;
	bnfa_state_t    *ps; /* transition list */
	bnfa_state_t    *pi; /* state indexes into ps */
	bnfa_state_t     ps_index=0;
	unsigned         nps;
	bnfa_state_t     full[BNFA_MAX_ALPHABET_SIZE];


	/* count total state transitions, account for state and control words  */
	nps = 0;
	for (k = 0; k < bnfa->num_states; k++)
	{
		nps++; /* state word */
		nps++; /* control word */

		/* count transitions */
		nc = 0;
		bnfa_list_conv_row_to_full(bnfa, (bnfa_state_t)k, full);
		for (i = 0; i < bnfa->alphabet_size; i++)
		{
			state = full[i] & BNFA_SPARSE_MAX_STATE;
			if (state != 0)
			{
				nc++;
			}
		}

		/* add in transition count */
		if ((k == 0 && bnfa->force_full_zero_state) 
				|| nc > BNFA_SPARSE_MAX_ROW_TRANSITIONS)
		{
			nps += BNFA_MAX_ALPHABET_SIZE;
		}
		else
		{
			for (i = 0; i < bnfa->alphabet_size; i++)
			{
				state = full[i] & BNFA_SPARSE_MAX_STATE;
				if (state != 0)
				{
					nps++;
				}
			}
		}
	}

	/* check if we have too many states + transitions */
	if (nps > BNFA_SPARSE_MAX_STATE)
	{
		/* Fatal */
		return -1;
	}

	/*
	   Alloc The Transition List - we need an array of bnfa_state_t items of size 'nps'
	   */
	ps = BNFA_MALLOC(nps*sizeof(bnfa_state_t));
	if (!ps)
	{
		/* Fatal */
		return -1;
	}
	bnfa->trans_list = ps;

	/*
	   State Index list for pi - we need an array of bnfa_state_t items of size 'NumStates'
	   */
	pi = BNFA_MALLOC(bnfa->num_states*sizeof(bnfa_state_t));
	if (!pi)
	{
		bnfa->trans_list = NULL;
		BNFA_FREE(ps);
		/* Fatal */
		return -1;
	}

	/*
	   Build the Transition List Array
	   */
	for (k = 0; k < bnfa->num_states; k++)
	{
		pi[k] = ps_index; /* save index of start of state 'k' */

		ps[ps_index] = k; /* save the state were in as the 1st word */

		ps_index++;  /* skip past state word */

		/* conver state 'k' to full format */
		bnfa_list_conv_row_to_full(bnfa, (bnfa_state_t)k, full);

		/* count transitions */
		nc = 0;
		for (i = 0; i < bnfa->alphabet_size; i++)
		{
			state = full[i] & BNFA_SPARSE_MAX_STATE;
			if (state != 0)
			{
				nc++;
			}
		}

		/* add a full state or a sparse state  */
		if ((k == 0 && bnfa->force_full_zero_state) ||
				nc > BNFA_SPARSE_MAX_ROW_TRANSITIONS)
		{
			/* set the control word */
			ps[ps_index] = BNFA_SPARSE_FULL_BIT;
			ps[ps_index] |= fail_state[k] & BNFA_SPARSE_MAX_STATE;
			if (bnfa->match_list[k])
			{
				ps[ps_index] |= BNFA_SPARSE_MATCH_BIT;
			}
			ps_index++;

			/* copy the transitions */
			bnfa_list_conv_row_to_full(bnfa, (bnfa_state_t)k, &ps[ps_index]);

			ps_index += BNFA_MAX_ALPHABET_SIZE;  /* add in 256 transitions */

		}
		else
		{
			/* set the control word */
			ps[ps_index]  = nc<<BNFA_SPARSE_COUNT_SHIFT ;
			ps[ps_index] |= fail_state[k]&BNFA_SPARSE_MAX_STATE;
			if (bnfa->match_list[k])
			{
				ps[ps_index] |= BNFA_SPARSE_MATCH_BIT;
			}
			ps_index++;

			/* add in the transitions */
			for (m=0, i=0; i<bnfa->alphabet_size && m<nc; i++)
			{
				state = full[i] & BNFA_SPARSE_MAX_STATE;
				if (state != 0)
				{
					ps[ps_index++] = (i<<BNFA_SPARSE_VALUE_SHIFT) | state;
					m++;
				}
			}
		}
	}

	/* sanity check we have not overflowed our buffer */
	if (ps_index > nps)
	{
		BNFA_FREE(pi);
		/* Fatal */
		return -1;
	}

	/*
	   Replace Transition states with Transition Indices.
	   This allows us to skip using next_state[] to locate the next state
	   This limits us to <16M transitions due to 24 bit state sizes, and the fact
	   we have now converted next-state fields to next-index fields in this array,
	   and we have merged the next-state and state arrays.
	   */
	ps_index = 0;
	for (k = 0; k < bnfa->num_states; k++)
	{
		if (pi[k] >= nps)
		{
			BNFA_FREE(pi);
			/* Fatal */
			return -1;
		}

		//ps_index = pi[k];  /* get index of next state */
		ps_index++;        /* skip state id */

		/* Full Format */
		if (ps[ps_index] & BNFA_SPARSE_FULL_BIT)
		{
			/* Do the fail-state */
			ps[ps_index] = (ps[ps_index] & 0xff000000) |
				(pi[ ps[ps_index] & BNFA_SPARSE_MAX_STATE ]) ;
			ps_index++;

			/* Do the transition-states */
			for (i = 0; i < BNFA_MAX_ALPHABET_SIZE; i++)
			{
				ps[ps_index] = (ps[ps_index] & 0xff000000) |
					(pi[ ps[ps_index] & BNFA_SPARSE_MAX_STATE ]) ;
				ps_index++;
			}
		}

		/* Sparse Format */
		else
		{
			nc = (ps[ps_index] & BNFA_SPARSE_COUNT_BITS)>>BNFA_SPARSE_COUNT_SHIFT;

			/* Do the cw = [cb | fail-state] */
			ps[ps_index] =  (ps[ps_index] & 0xff000000) |
				(pi[ ps[ps_index] & BNFA_SPARSE_MAX_STATE ]);
			ps_index++;

			/* Do the transition-states */
			for (i = 0; i < nc; i++)
			{
				ps[ps_index] = (ps[ps_index] & 0xff000000) |
					(pi[ ps[ps_index] & BNFA_SPARSE_MAX_STATE ]);
				ps_index++;
			}
		}

		/* check for buffer overflow again */
		if (ps_index > nps)
		{
			BNFA_FREE(pi);
			/* Fatal */
			return -1;
		}

	}

	BNFA_FREE(pi);

	return 0;
}

void bnfa_print(bnfa_struct_t *bnfa)
{
	int               k;
	bnfa_match_node_t  ** match_list;
	bnfa_match_node_t   * mlist;
	int              ps_index=0;
	bnfa_state_t      * ps=0;

	if(!bnfa)
		return;

	match_list = bnfa->match_list;

	if (!bnfa->num_states)
		return;

	if (bnfa->format == BNFA_SPARSE)
	{
		printf("Print NFA-SPARSE state machine : %d active states\n", bnfa->num_states);
		ps = bnfa->trans_list;
		if (!ps)
			return;
	}

	else if (bnfa->format ==BNFA_FULL)
	{
		printf("Print NFA-FULL state machine : %d active states\n", bnfa->num_states);
	}


	for (k = 0; k < bnfa->num_states; k++)
	{
		printf(" state %-4d fmt=%d ",k,bnfa->format);

		if (bnfa->format == BNFA_SPARSE)
		{
			unsigned i,cw,fs,nt,fb,mb;

			ps_index++; /* skip state number */

			cw = ps[ps_index]; /* control word  */
			fb = (cw &  BNFA_SPARSE_FULL_BIT)>>BNFA_SPARSE_VALUE_SHIFT;  /* full storage bit */
			mb = (cw &  BNFA_SPARSE_MATCH_BIT)>>BNFA_SPARSE_VALUE_SHIFT; /* matching state bit */
			nt = (cw &  BNFA_SPARSE_COUNT_BITS)>>BNFA_SPARSE_VALUE_SHIFT;/* number of transitions 0-63 */
			fs = (cw &  BNFA_SPARSE_MAX_STATE)>>BNFA_SPARSE_VALUE_SHIFT; /* fail state */

			ps_index++;  /* skip control word */

			printf("mb=%3u fb=%3u fs=%-4u ",mb,fb,fs);

			if (fb)
			{
				printf(" nt=%-3d : ",bnfa->alphabet_size);

				for (i=0; i<(unsigned)bnfa->alphabet_size; i++, ps_index++)
				{
					if (ps[ps_index] == 0) 
						continue;

					if (isascii((int)i) && isprint((int)i))
						printf("%3c->%-6d\t",i,ps[ps_index]);
					else
						printf("%3d->%-6d\t",i,ps[ps_index]);
				}
			}
			else
			{
				printf(" nt=%-3d : ",nt);

				for (i=0; i<nt; i++, ps_index++)
				{
					if( isascii(ps[ps_index]>>BNFA_SPARSE_VALUE_SHIFT) &&
							isprint(ps[ps_index]>>BNFA_SPARSE_VALUE_SHIFT) )
						printf("%3c->%-6d\t",ps[ps_index]>>BNFA_SPARSE_VALUE_SHIFT,ps[ps_index] & BNFA_SPARSE_MAX_STATE);
					else
						printf("%3d->%-6d\t",ps[ps_index]>>BNFA_SPARSE_VALUE_SHIFT,ps[ps_index] & BNFA_SPARSE_MAX_STATE);
				}
			}
		}
		else if (bnfa->format == BNFA_FULL)
		{
			int          i;
			bnfa_state_t    state;
			bnfa_state_t  * p;
			bnfa_state_t ** NextState;

			NextState = (bnfa_state_t **)bnfa->next_state;
			if( !NextState )
				continue;

			p = NextState[k];

			printf("fs=%-4d nc=256 ",bnfa->fail_state[k]);

			for( i=0; i<bnfa->alphabet_size; i++ )
			{
				state = p[i];

				if( state != 0 && state != BNFA_FAIL_STATE )
				{
					if( isascii(i) && isprint(i) )
						printf("%3c->%-5d\t",i,state);
					else
						printf("%3d->%-5d\t",i,state);
				}
			}
		}

		printf("\n");

		if (match_list[k])
			printf("---match_list For State %d\n",k);

		for (mlist = match_list[k];
				mlist!= NULL;
				mlist = mlist->next )
		{
			bnfa_pattern_t * pat;
			pat = (bnfa_pattern_t*)mlist->data;
			printf("---pattern : %.*s\n",pat->n,pat->patrn);
		}
	}
}



/*
 *  Create a new AC state machine
 */
bnfa_struct_t * bnfa_new(void (*priv_data_free)(void *p))
{
	bnfa_struct_t *p;

	init_xlatcase ();

	p = BNFA_MALLOC(sizeof(bnfa_struct_t));
	if (!p)
		return NULL;

	if (p)
	{
		p->opt                = 0;
		p->format             = BNFA_SPARSE;
		p->alphabet_size      = BNFA_MAX_ALPHABET_SIZE;
		p->force_full_zero_state = 1;
		p->priv_data_free     = priv_data_free;
	}

	return p;
}

void bnfa_set_opt(bnfa_struct_t  *p, int flag)
{
	p->opt = flag;
}

/*
 *   Fee all memory
 */
void bnfa_free(bnfa_struct_t *bnfa)
{
	int i;
	bnfa_pattern_t * patrn, *ipatrn;
	bnfa_match_node_t   * mlist, *ilist;

	if (bnfa == NULL)
		return;

	for (i = 0; i < bnfa->num_states; i++)
	{
		/* free match list entries */
		mlist = bnfa->match_list[i];

		while (mlist)
		{
			ilist = mlist;
			mlist = mlist->next;
			BNFA_FREE(ilist);
		}
		bnfa->match_list[i] = 0;

		/* free next state entries */
		if (bnfa->format == BNFA_FULL)
		{
			if (bnfa->next_state[i])
			{
				BNFA_FREE(bnfa->next_state[i]);
			}
		}
	}

	/* Free patterns */
	patrn = bnfa->patterns;
	while (patrn)
	{
		ipatrn=patrn;
		patrn=patrn->next;
		BNFA_FREE(ipatrn->patrn);
		if (bnfa->priv_data_free && ipatrn->private)
			bnfa->priv_data_free(ipatrn->private);
		BNFA_FREE(ipatrn);
	}

	/* Free arrays */
	BNFA_FREE(bnfa->fail_state);
	BNFA_FREE(bnfa->match_list);
	BNFA_FREE(bnfa->next_state);
	BNFA_FREE(bnfa->trans_list);
	BNFA_FREE(bnfa); 
}

/*
 *   Add a pattern to the pattern list
 */
int bnfa_add_pattern(bnfa_struct_t *p,
		unsigned char *pat,
		int n,
		int nocase,
		int offset,
		int depth,
		void * priv_data,
		int id)
{
	bnfa_pattern_t *plist;

	if (p == NULL)
		return -1;

	plist = BNFA_MALLOC(sizeof(bnfa_pattern_t));
	if (!plist) 
		return -1;

	plist->patrn = BNFA_MALLOC(n);
	if (!plist->patrn)
	{
		BNFA_FREE(plist);
		return -1;
	}

	memcpy(plist->patrn, pat, n);

	plist->n        = n;
	plist->nocase   = nocase;
	plist->offset   = offset;
	plist->depth    = depth;
	plist->id       = id;
	plist->private  = priv_data;

	plist->next = p->patterns; /* insert at front of list */
	p->patterns = plist;

	p->pattern_cnt++;

	return 0;
}

/*
 *   Compile the patterns into an nfa state machine
 */
int bnfa_compile(bnfa_struct_t *bnfa)
{
	bnfa_pattern_t *plist;
	bnfa_match_node_t **tmpmatch_list;
	unsigned int match_states;
	int i;

	if (bnfa == NULL)
		return -1;

	/* Count number of states */
	for (plist = bnfa->patterns; plist != NULL; plist = plist->next)
	{
		bnfa->max_states += plist->n;
	}
	bnfa->max_states++; /* one extra */

	/* Alloc a List based State Transition table */
	bnfa->trans_table = BNFA_MALLOC(sizeof(bnfa_trans_node_t*) * bnfa->max_states);
	if (!bnfa->trans_table)
	{
		return -1;
	}

	/* Alloc a match_list table - this has a list of pattern matches for each state */
	bnfa->match_list = BNFA_MALLOC(sizeof(void*) * bnfa->max_states);
	if (!bnfa->match_list)
	{
		BNFA_FREE(bnfa->trans_table);
		return -1;
	}

	/* Add each Pattern to the State Table - This forms a keyword trie using lists */
	bnfa->num_states = 0;
	for (plist = bnfa->patterns; plist != NULL; plist = plist->next)
	{
		bnfa_add_pattern_states(bnfa, plist);
	}
	bnfa->num_states++;

	if (bnfa->num_states > BNFA_SPARSE_MAX_STATE)
	{
		bnfa_free(bnfa);
		return -1;
	}

	/* ReAlloc a smaller match_list table -  only need NumStates  */
	tmpmatch_list = bnfa->match_list;

	bnfa->match_list = BNFA_MALLOC(sizeof(void*) * bnfa->num_states);
	if (!bnfa->match_list)
	{
		bnfa->match_list = tmpmatch_list;
		bnfa_free(bnfa);
		return -1;
	}

	memcpy(bnfa->match_list, tmpmatch_list, sizeof(void*) * bnfa->num_states);

	BNFA_FREE(tmpmatch_list);

	/* Alloc a failure state table -  only need NumStates */
	bnfa->fail_state = BNFA_MALLOC(sizeof(bnfa_state_t) * bnfa->num_states);
	if (!bnfa->fail_state)
	{
		bnfa_free(bnfa);
		return -1;
	}

	if (bnfa->format == BNFA_FULL)
	{
		/* Alloc a state transition table -  only need NumStates  */
		bnfa->next_state = BNFA_MALLOC(sizeof(bnfa_state_t*) * bnfa->num_states);
		if (!bnfa->next_state)
		{
			bnfa_free(bnfa);
			return -1;
		}
	}

	/* Build the nfa w/failure states - time the nfa construction */
	if (bnfa_build_nfa(bnfa))
	{
		bnfa_free(bnfa);
		return -1;
	}

	/* Convert nfa storage format from list to full or sparse */
	if (bnfa->format == BNFA_SPARSE)
	{
		if (bnfa_conv_list_to_csparse_array(bnfa) )
		{
			bnfa_free(bnfa);
			return -1;
		}
		BNFA_FREE(bnfa->fail_state);
		bnfa->fail_state = NULL;
	}
	else if (bnfa->format == BNFA_FULL)
	{
		if (bnfa_conv_list_to_full(bnfa))
		{
			bnfa_free(bnfa);
			return -1;
		}
	}
	else
	{
		bnfa_free(bnfa);
		return -1;
	}

	/* Free up the Table Of Transition Lists */
	bnfa_list_free_table(bnfa);

	/* Count states with Pattern Matches */
	match_states = 0;
	for (i = 0; i < bnfa->num_states; i++)
	{
		if (bnfa->match_list[i])
			match_states++;
	}

	bnfa->match_states = match_states;

	/*bnfa_print(bnfa); */

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
 *   Full Matrix Format Search
 */
static inline unsigned bnfa_search_full_nfa(bnfa_struct_t *bnfa, 
		unsigned char *tx, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, bnfa_state_t state, int *current_state)
{
	unsigned char      * tend;
	unsigned char      * t;
	unsigned char        tchar;
	int                  index;
	bnfa_state_t      ** next_state= bnfa->next_state;
	bnfa_state_t       * fail_state= bnfa->fail_state;
	bnfa_match_node_t ** match_list= bnfa->match_list;
	bnfa_state_t       * pcs;
	bnfa_match_node_t  * mlist;
	bnfa_pattern_t     * patrn;
	int                  res;
	unsigned             last_match = LAST_STATE_INIT;
	unsigned             last_match_saved = LAST_STATE_INIT;

	t    = tx;
	tend = t + n;

	for (; t < tend; t++)
	{
		tchar = xlatcase[*t];

		for (;;)
		{
			pcs = next_state[state];
			if (pcs[tchar] == 0 && state > 0)
			{
				state = fail_state[state];
			}
			else
			{
				state = pcs[tchar];
				break;
			}
		}

		if (state)
		{
			if (state == last_match)
				continue;

			last_match_saved = last_match;
			last_match = state;

			mlist = match_list[state];

			while (mlist)
			{
				patrn = (bnfa_pattern_t*)mlist->data;
				index = t - tx - patrn->n + 1;

				if (pattern_attr_is_match(tx, index,
						patrn->patrn, patrn->n,
						patrn->nocase,
						patrn->offset,
						patrn->depth) == 0)
				{
					mlist = mlist->next;
					continue;
				}

				if (match == NULL)
				{
					*current_state = state;
					return 1;
				}

				res = match(patrn->private, index, patrn->id, data);
				if (res > 0)
				{
					*current_state = state;
					return 1;
				}
				else if (res < 0)
				{
					last_match = last_match_saved;
				}
			}
		}
	}
	*current_state = state;
	return 0;
}

/*
   binary array search on sparse transition array

   O(logN) search times..same as a binary tree.
   data must be in sorted order in the array.

return:  = -1 => not found
>= 0  => index of element 'val'

notes:
val is tested against the high 8 bits of the 'a' array entry,
this is particular to the storage format we are using.
*/
static inline int bnfa_binearch(bnfa_state_t *a, int a_len, int val)
{
	int m, l, r;
	int c;

	l = 0;
	r = a_len - 1;

	while (r >= l)
	{
		m = (r + l) >> 1;

		c = a[m] >> BNFA_SPARSE_VALUE_SHIFT;

		if (val == c)
		{
			return m;
		}

		else if (val <  c)
		{
			r = m - 1;
		}

		else /* val > c */
		{
			l = m + 1;
		}
	}
	return -1;
}


/*
 *   Sparse format for state table using single array storage
 *
 *   word 1: state
 *   word 2: control-word = cb<<24| fs
 *           cb    : control-byte
 *                : mb | fb | nt
 *                mb : bit 8 set if match state, zero otherwise
 *                fb : bit 7 set if using full format, zero otherwise
 *                nt : number of transitions 0..63 (more than 63 requires full format)
 *            fs: failure-transition-state
 *   word 3+: byte-value(0-255) << 24 | transition-state
 */
static inline unsigned bnfa_get_next_state_csparse_nfa(bnfa_state_t * pcx, 
		unsigned sindex, unsigned  input)
{
	int k;
	int nc;
	int index;
	register bnfa_state_t *pcs;

	for (;;)
	{
		pcs = pcx + sindex + 1; /* skip state-id == 1st word */

		if (pcs[0] & BNFA_SPARSE_FULL_BIT)
		{
			if (sindex == 0)
			{
				return pcs[1+input] & BNFA_SPARSE_MAX_STATE;
			}
			else
			{
				if (pcs[1+input] & BNFA_SPARSE_MAX_STATE)
					return pcs[1+input] & BNFA_SPARSE_MAX_STATE;
			}
		}
		else
		{
			nc = (pcs[0]>>BNFA_SPARSE_COUNT_SHIFT) & BNFA_SPARSE_MAX_ROW_TRANSITIONS;
			if (nc > BNFA_SPARSE_LINEAR_SEARCH_LIMIT)
			{
				/* binary search... */
				index = bnfa_binearch(pcs+1, nc, input);
				if (index >= 0)
				{
					return pcs[index+1] & BNFA_SPARSE_MAX_STATE;
				}
			}
			else
			{
				/* linear search... */
				for (k = 0; k < nc; k++)
				{
					if ((pcs[k+1]>>BNFA_SPARSE_VALUE_SHIFT) == input)
					{
						return pcs[k+1] & BNFA_SPARSE_MAX_STATE;
					}
				}
			}
		}

		/* no transition found ... get the failure state and try again  */
		sindex = pcs[0] & BNFA_SPARSE_MAX_STATE;
	}
}

/*
 *  Per Pattern case search, case is on per pattern basis
 *  standard snort search
 *
 *  note: index is not used by snort, so it's commented
 */
static inline unsigned bnfa_search_csparse_nfa(bnfa_struct_t *bnfa, 
		unsigned char *tx, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, unsigned sindex, int *current_state)
{
	bnfa_match_node_t  * mlist;
	unsigned char      * tend;
	unsigned char      * t;
	unsigned char        tchar;
	int                  index;
	bnfa_match_node_t ** match_list = bnfa->match_list;
	bnfa_pattern_t     * patrn;
	bnfa_state_t       * trans_list = bnfa->trans_list;
	unsigned             last_match=LAST_STATE_INIT;
	unsigned             last_match_saved=LAST_STATE_INIT;
	int                  res;

	t    = tx;
	tend = t + n;

	for (; t < tend; t++)
	{
		tchar = xlatcase[*t];

		/* Transition to next state index */
		sindex = bnfa_get_next_state_csparse_nfa(trans_list,sindex,tchar);

		/* Log matches in this state - if any */
		if (sindex && (trans_list[sindex+1] & BNFA_SPARSE_MATCH_BIT))
		{
			if (sindex == last_match)
				continue;

			last_match_saved = last_match;
			last_match = sindex;

			mlist = match_list[trans_list[sindex]];
			while (mlist)
			{
				patrn = (bnfa_pattern_t*)mlist->data;
				index = t - tx - patrn->n + 1;

				if (pattern_attr_is_match(tx, index,
						patrn->patrn, patrn->n,
						patrn->nocase,
						patrn->offset,
						patrn->depth) == 0)
				{
					mlist = mlist->next;
					continue;
				}

				/* Don't do anything specific for case sensitive patterns and not,
				 * since that will be covered by the rule tree itself.  Each tree
				 * might have both case sensitive & case insensitive patterns.
				 */
				if (match == NULL)
				{
					*current_state = sindex;
					return 1;
				}

				res = match(patrn->private, index, patrn->id, data);
				if (res > 0)
				{
					*current_state = sindex;
					return 1;
				}
				else if (res < 0)
				{
					last_match = last_match_saved;
				}
				else
					mlist = mlist->next;
			}
		}
	}
	*current_state = sindex;
	return 0;
}

/*
 *  BNFA Search Function
 *
 *  bnfa   - state machine
 *  Tx     - text buffer to search
 *  n      - number of bytes in Tx
 *  Match  - function to call when a match is found
 *  data   - user supplied data that is passed to the Match function
 *  sindex - state tracker, set value to zero to reset the state machine,
 *            zero should be the value passed in on the 1st buffer or each buffer
 *           that is to be analyzed on its own, the state machine updates this
 *            during searches. This allows for sequential buffer searchs without
 *            reseting the state machine. Save this value as returned from the
 *            previous search for the next search.
 *
 *  returns
 *    The state or sindex of the state machine. This can than be passed back
 *   in on the next search, if desired.
 */

int bnfa_search(bnfa_struct_t *bnfa, unsigned char *tx, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state)
{
	int ret = 0;
	int state = 0;

	if (!current_state)
	{
		return 0;
	}

	if (*current_state < 0)
		*current_state = 0;

	state = (unsigned int)*current_state;

	if (bnfa->format == BNFA_SPARSE)
	{
		ret = bnfa_search_csparse_nfa(bnfa, tx, n,
					match, data, state, current_state);
	}
	else if (bnfa->format == BNFA_FULL)
	{
		ret = bnfa_search_full_nfa(bnfa, tx, n,
				match, data, (bnfa_state_t)state, current_state);
	}

	return ret;
}

int bnfa_pattern_count(bnfa_struct_t *p)
{
	return p->pattern_cnt;
}

