/*
 * acsm2 code
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
#include "acsm2.h"

#ifndef USHRT_MAX
#define USHRT_MAX     ((u_int16_t)(~0U))
#endif

#ifndef UCHAR_MAX
#define UCHAR_MAX     ((u_int8_t)(~0U))
#endif

#define AC_MALLOC(n) mem_alloc(n)
#define AC_FREE(p) \
	do { \
		if (p) { \
			free(p); \
			p = NULL; \
		} \
	} while(0)


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
		xlatcase[i] = (unsigned char)toupper(i);
	}
	xlatcase_init = 1;
}
/*
 *    Case Conversion
 */
static inline void convert_case_ex(unsigned char *d, unsigned char *s, int m)
{
	int i;

	for (i = 0; i < m; i++)
	{
		d[i] = xlatcase[s[i]];
	}
}

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
	QNODE * head, *tail;
	int count;
} QUEUE;

/*
 *   Initialize the queue
 */
static void queue_init(QUEUE * s)
{
	s->head = s->tail = NULL;
	s->count = 0;
}

/*
 *  Find a State in the queue
 */
static int queue_find(QUEUE *s, int state)
{
	QNODE * q;
	q = s->head;
	while (q)
	{
		if (q->state == state) 
			return 1;
		q = q->next;
	}
	return 0;
}

/*
 *  Add Tail Item to queue (FiFo/LiLo)
 */
static void queue_add(QUEUE *s, int state)
{
	QNODE *q;

	if (queue_find(s, state))
		return;

	if (!s->head)
	{
		q = s->tail = s->head = AC_MALLOC(sizeof(QNODE));
		if (q == NULL)
			return;
		q->state = state;
		q->next = 0;
	}
	else
	{
		q = AC_MALLOC(sizeof(QNODE));
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
		AC_FREE(q);
	}

	return state;
}


/*
 *   Return items in the queue
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
 *  Get Next State-NFA
 */
static int list_get_next_state(ACSM_STRUCT2 *acsm, int state, int input)
{
	trans_node_t *t = acsm->trans_table[state];

	while (t)
	{
		if (t->key == (acstate_t)input)
		{
			return t->next_state;
		}
		t = t->next;
	}

	if (state == 0)
		return 0;

	return ACSM_FAIL_STATE2;
}

/*
 *  Get Next State-DFA
 */
static int list_get_next_state2(ACSM_STRUCT2 *acsm, int state, int input)
{
	trans_node_t * t = acsm->trans_table[state];

	while (t)
	{
		if (t->key == (acstate_t)input)
		{
			return t->next_state;
		}
		t = t->next;
	}

	return 0;
}

/*
 *  Put Next State - Head insertion, and transition updates
 */
static int list_put_next_state(ACSM_STRUCT2 *acsm, 
		int state, int input, int next_state )
{
	trans_node_t *p;
	trans_node_t *tnew;

	/* Check if the transition already exists, if so just update the next_state */
	p = acsm->trans_table[state];
	while (p)
	{
		/* transition already exists- reset the next state */
		if (p->key == (acstate_t)input)
		{
			p->next_state = next_state;
			return 0;
		}
		p = p->next;
	}

	/* Definitely not an existing transition - add it */
	tnew = AC_MALLOC(sizeof(trans_node_t));
	if (!tnew)
		return -1;

	tnew->key = input;
	tnew->next_state = next_state;
	tnew->next = 0;

	tnew->next = acsm->trans_table[state];
	acsm->trans_table[state] = tnew;

	acsm->num_trans++;

	return 0;
}

/*
 *   Free the entire transition table
 */
static int list_free_trans_table(ACSM_STRUCT2 *acsm)
{
	int i;
	trans_node_t *t, *p;

	if (acsm->trans_table == NULL)
		return 0;

	for (i = 0; i < acsm->max_states; i++)
	{
		t = acsm->trans_table[i];
		while (t != NULL)
		{
			p = t->next;
			AC_FREE(t);
			t = p;
		}
	}

	AC_FREE(acsm->trans_table);
	acsm->trans_table = NULL;
	return 0;
}

/*
 *   Converts row of states from list to a full vector format
 */
static int list_conv_to_full(ACSM_STRUCT2 *acsm, acstate_t state, acstate_t *full)
{
	int tcnt = 0;
	trans_node_t *t = acsm->trans_table[state];

	memset(full, 0, acsm->sizeofstate * acsm->alphabet_size);

	if (t == NULL)
		return 0;

	while (t != NULL)
	{
		switch (acsm->sizeofstate)
		{
		case 1:
			*((u_int8_t *)full + t->key) = (u_int8_t)t->next_state;
			break;
		case 2:
			*((u_int16_t *)full + t->key) = (u_int16_t)t->next_state;
			break;
		default:
			full[t->key] = t->next_state;
			break;
		}

		tcnt++;
		t = t->next;
	}

	return tcnt;
}


/*
 *  Add a pattern to the list of patterns terminated at this state.
 *  Insert at front of list.
 */
static void add_match_list_entry(ACSM_STRUCT2 *acsm, int state, ACSM_PATTERN2 *px)
{
	ACSM_PATTERN2 * p;

	p = AC_MALLOC(sizeof (ACSM_PATTERN2));
	if (p == NULL)
		return;

	memcpy(p, px, sizeof (ACSM_PATTERN2));

	p->next = acsm->match_list[state];
	acsm->match_list[state] = p;
}

static ACSM_PATTERN2 * copy_match_list_entry(ACSM_PATTERN2 *px)
{
	ACSM_PATTERN2 *p;

	p = AC_MALLOC(sizeof(*p));
	if (p == NULL)
		return NULL;

	memcpy(p, px, sizeof(*p));
	return p;
}


static void add_pattern_states(ACSM_STRUCT2 *acsm, ACSM_PATTERN2 *p)
{
	int state;
	int next;
	int n;
	unsigned char *pattern;
	unsigned char c;

	n = p->n;
	pattern = p->patrn;
	state = 0;

	/*
	 *  Match up pattern with existing states
	 */
	for (; n > 0; pattern++, n--)
	{
		c = xlatcase[*pattern];
		next = list_get_next_state(acsm, state, c);
		if ((acstate_t)next == ACSM_FAIL_STATE2 || next == 0)
		{
			break;
		}
		state = next;
	}

	/*
	 *   Add new states for the rest of the pattern bytes, 1 state per byte
	 */
	for (; n > 0; pattern++, n--)
	{
		c = xlatcase[*pattern];
		acsm->num_states++;
		list_put_next_state(acsm, state, c, acsm->num_states);
		state = acsm->num_states;
	}

	add_match_list_entry(acsm, state, p);
}

/*
 *   Build A Non-Deterministic Finite Automata
 *   The keyword state table must already be built, via AddPatternStates().
 */
static void build_NFA(ACSM_STRUCT2 *acsm)
{
	int r, s, i;
	QUEUE q, *queue = &q;
	acstate_t *fail_state = acsm->fail_state;
	ACSM_PATTERN2 *px;
	ACSM_PATTERN2 *mlist;
	ACSM_PATTERN2 **match_list = acsm->match_list;

	queue_init (queue);

	for (i = 0; i < acsm->alphabet_size; i++)
	{
		s = list_get_next_state2(acsm,0,i);
		if( s )
		{
			queue_add (queue, s);
			fail_state[s] = 0;
		}
	}

	/* Build the fail state successive layer of transitions */
	while (queue_count (queue) > 0)
	{
		r = queue_remove (queue);

		/* Find Final States for any Failure */
		for (i = 0; i < acsm->alphabet_size; i++)
		{
			int fs, next;

			s = list_get_next_state(acsm, r, i);
			if( (acstate_t)s != ACSM_FAIL_STATE2 )
			{
				queue_add (queue, s);

				fs = fail_state[r];

				next = list_get_next_state(acsm, fs, i);
				while ((acstate_t)next == ACSM_FAIL_STATE2 )
				{
					fs = fail_state[fs];
					next = list_get_next_state(acsm, fs, i);
				}

				fail_state[s] = next;

				for (mlist = match_list[next];
					mlist;
					mlist = mlist->next)
				{
					px = copy_match_list_entry(mlist);
					if (px == NULL)
						continue;
					px->next = match_list[s];
					match_list[s] = px;
				}
			}
		}
	}

	/* Clean up the queue */
	queue_free (queue);
}

/*
 *   Build Deterministic Finite Automata from the NFA
 */
static void convert_NFA_to_DFA(ACSM_STRUCT2 *acsm)
{
	int i, r, s, cfail_state;
	QUEUE q, *queue = &q;
	acstate_t *fail_state = acsm->fail_state;

	queue_init(queue);

	for (i = 0; i < acsm->alphabet_size; i++)
	{
		s = list_get_next_state(acsm, 0, i);
		if (s != 0)
		{
			queue_add(queue, s);
		}
	}

	/* Start building the next layer of transitions */
	while (queue_count(queue) > 0)
	{
		r = queue_remove(queue);

		/* Process this states layer */
		for (i = 0; i < acsm->alphabet_size; i++)
		{
			s = list_get_next_state(acsm,r,i);

			if( (acstate_t)s != ACSM_FAIL_STATE2 && s!= 0)
			{
				queue_add(queue, s);
			}
			else
			{
				cfail_state = 
				list_get_next_state(acsm, fail_state[r], i);

				if (cfail_state != 0 
					&& (acstate_t)cfail_state != ACSM_FAIL_STATE2 )
				{
					list_put_next_state(acsm, r, i, cfail_state);
				}
			}
		}
	}

	/* Clean up the queue */
	queue_free(queue);
}

/*
 *
 *  Convert a row lists for the state table to a full vector format
 *
 */
static int conv_list_to_full(ACSM_STRUCT2 *acsm)
{
	acstate_t k;
	acstate_t *p;
	acstate_t **next_state = acsm->next_state;

	for (k = 0; k < (acstate_t)acsm->num_states; k++)
	{
		p = AC_MALLOC(acsm->sizeofstate * (acsm->alphabet_size + 2));
		if (p == NULL)
			return -1;

		switch (acsm->sizeofstate)
		{
		case 1:
			list_conv_to_full(acsm, k, (acstate_t *)((u_int8_t *)p + 2));
			*((u_int8_t *)p) = ACF_FULL;
			*((u_int8_t *)p + 1) = 0;
			break;
		case 2:
			list_conv_to_full(acsm, k, (acstate_t *)((u_int16_t *)p + 2));
			*((u_int16_t *)p) = ACF_FULL;
			*((u_int16_t *)p + 1) = 0;
			break;
		default:
			list_conv_to_full(acsm, k, (p + 2));
			p[0] = ACF_FULL;
			p[1] = 0;
			break;
		}

		next_state[k] = p;
	}

	return 0;
}

/*
 *   Convert DFA memory usage from list based storage to a sparse-row storage.
 *
 *   The Sparse format allows each row to be either full or sparse formatted.  If the sparse row has
 *   too many transitions, performance or space may dictate that we use the standard full formatting
 *   for the row.  More than 5 or 10 transitions per state ought to really whack performance. So the
 *   user can specify the max state transitions per state allowed in the sparse format.
 *
 *   Standard Full Matrix Format
 *   ---------------------------
 *   acstate_t ** next_state ( 1st index is row/state, 2nd index is column=event/input)
 *
 *   example:
 *
 *        events -> a b c d e f g h i j k l m n o p
 *   states
 *     N            1 7 0 0 0 3 0 0 0 0 0 0 0 0 0 0
 *
 *   Sparse Format, each row : Words     Value
 *                            1-1       fmt(0-full,1-sparse,2-banded,3-sparsebands)
 *                            2-2       bool match flag (indicates this state has pattern matches)
 *                            3-3       sparse state count ( # of input/next-state pairs )
 *                            4-3+2*cnt 'input,next-state' pairs... each sizof(acstate_t)
 *
 *   above example case yields:
 *     Full Format:    0, 1 7 0 0 0 3 0 0 0 0 0 0 0 0 0 0 ...
 *     Sparse format:  1, 3, 'a',1,'b',7,'f',3  - uses 2+2*ntransitions (non-default transitions)
 */
static int conv_full_DFA_To_sparse(ACSM_STRUCT2 *acsm)
{
	int cnt, m, k, i;
	acstate_t  *p, state, maxstates = 0;
	acstate_t **next_state = acsm->next_state;
	acstate_t *full;
	int size;

	full = AC_MALLOC(sizeof(acstate_t) * MAX_ALPHABET_SIZE);
	if (full == NULL)
		return -1;

	for (k = 0; k < acsm->num_states; k++)
	{
		cnt = 0;

		list_conv_to_full(acsm, (acstate_t)k, full );

		for (i = 0; i < acsm->alphabet_size; i++)
		{
			state = full[i];
			if (state != 0 && state != ACSM_FAIL_STATE2 )
				cnt++;
		}

		if (cnt > 0)
			maxstates++;

		if (k == 0 || cnt > acsm->sparse_max_row_nodes )
		{
			size = sizeof(acstate_t) * (acsm->alphabet_size + 2);
			p = AC_MALLOC(size);
			if (p == NULL)
			{
				AC_FREE(full);
				return -1;
			}

			p[0] = ACF_FULL;
			p[1] = 0;
			memcpy(&p[2], full, size - 2);
		}
		else
		{
			p = AC_MALLOC(sizeof(acstate_t) * (3 + 2 * cnt));
			if (p == NULL)
			{
				AC_FREE(full);
				return -1;
			}

			m = 0;
			p[m++] = ACF_SPARSE;
			p[m++] = 0;
			p[m++] = cnt;

			for(i = 0; i < acsm->alphabet_size; i++)
			{
				state = full[i];
				if( state != 0 && state != ACSM_FAIL_STATE2 )
				{
					p[m++] = i;
					p[m++] = state;
				}
			}
		}

		next_state[k] = p;
	}

	AC_FREE(full);
	return 0;
}
/*
   Convert Full matrix to Banded row format.

   Word     values
   1        2  -> banded
   2        n  number of values
   3        i  index of 1st value (0-256)
   4 - 3+n  next-state values at each index

 */
static int conv_full_DFA_to_banded(ACSM_STRUCT2 *acsm)
{
	int first = -1, last;
	acstate_t *p, state;
	acstate_t *full;
	acstate_t **next_state = acsm->next_state;
	int cnt,m,k,i;

	full = AC_MALLOC(sizeof(acstate_t) * MAX_ALPHABET_SIZE);
	if (full == NULL)
	{
		return -1;
	}

	for (k = 0; k < acsm->num_states; k++)
	{
		cnt = 0;

		list_conv_to_full(acsm, (acstate_t)k, full);

		first = -1;
		last = -2;
		for (i = 0; i < acsm->alphabet_size; i++)
		{
			state = full[i];

			if (state != 0 && state != ACSM_FAIL_STATE2)
			{
				if (first < 0)
					first = i;
				last = i;
			}
		}

		/* calc band width */
		cnt = last - first + 1;

		p = AC_MALLOC(sizeof(acstate_t) * (4 + cnt));
		if (p == NULL)
		{
			AC_FREE(full);
			return -1;
		}

		m = 0;
		p[m++] = ACF_BANDED;
		p[m++] = 0;   /* no matches */
		p[m++] = cnt;
		p[m++] = first;

		for (i = first; i <= last; i++)
		{
			p[m++] = full[i];
		}

		next_state[k] = p;
	}

	AC_FREE(full);
	return 0;
}

/*
 *   Convert full matrix to Sparse Band row format.
 *
 *   next  - Full formatted row of next states
 *   asize - size of alphabet
 *   zcnt - max number of zeros in a run of zeros in any given band.
 *
 *  Word Values
 *  1    ACF_SPARSEBANDS
 *  2    number of bands
 *  repeat 3 - 5+ ....once for each band in this row.
 *  3    number of items in this band*  4    start index of this band
 *  5-   next-state values in this band...
 */
static int calc_sparse_bands(acstate_t *next, 
		int *begin, int *end, int asize, int zmax )
{
	int i, nbands, zcnt, last = 0;
	acstate_t state;

	nbands = 0;
	for (i = 0; i < asize; i++)
	{
		state = next[i];
		if (state != 0 && state != ACSM_FAIL_STATE2)
		{
			begin[nbands] = i;
			zcnt = 0;
			for (; i < asize; i++)
			{
				state = next[i];
				if (state == 0 || state == ACSM_FAIL_STATE2)
				{
					zcnt++;
					if (zcnt > zmax)
						break;
				}
				else
				{
					zcnt = 0;
					last = i;
				}
			}
			end[nbands++] = last;
		}
	}
	return nbands;
}


/*
 *   Sparse Bands
 *
 *   Row Format:
 *   Word
 *   1    SPARSEBANDS format indicator
 *   2    bool indicates a pattern match in this state
 *   3    number of sparse bands
 *   4    number of elements in this band
 *   5    start index of this band
 *   6-   list of next states
 *
 *   m    number of elements in this band
 *   m+1  start index of this band
 *   m+2- list of next states
 */
static int conv_full_DFA_to_sparse_bands(ACSM_STRUCT2 *acsm)
{
	acstate_t *p;
	acstate_t **next_state = acsm->next_state;
	int cnt, m, k, i;
	int zcnt = acsm->sparse_max_zcnt;
	int *band_begin;
	int *band_end;
	int nbands, j;
	acstate_t *full;

	band_begin = AC_MALLOC(sizeof(int) * MAX_ALPHABET_SIZE);
	if (band_begin == NULL)
		return -1;
	band_end = AC_MALLOC(sizeof(int) * MAX_ALPHABET_SIZE);
	if (band_end == NULL)
	{
		AC_FREE(band_begin);
		return -1;
	}
	full = AC_MALLOC(sizeof(acstate_t) * MAX_ALPHABET_SIZE);
	if (full == NULL)
	{
		AC_FREE(band_begin);
		AC_FREE(band_end);
		return -1;
	}

	for (k = 0; k < acsm->num_states; k++)
	{
		cnt = 0;

		list_conv_to_full(acsm, (acstate_t)k, full);

		nbands = calc_sparse_bands(full, band_begin, band_end, 
				acsm->alphabet_size, zcnt);

		/* calc band width space*/
		cnt = 3;
		for (i = 0; i < nbands; i++)
		{
			cnt += 2;
			cnt += band_end[i] - band_begin[i] + 1;
		}

		p = AC_MALLOC(sizeof(acstate_t) * (cnt));
		if (p == NULL)
		{
			AC_FREE(band_begin);
			AC_FREE(band_end);
			AC_FREE(full);
			return -1;
		}

		m = 0;
		p[m++] = ACF_SPARSEBANDS;
		p[m++] = 0; /* no matches */
		p[m++] = nbands;

		for (i = 0; i < nbands; i++)
		{
			p[m++] = band_end[i] - band_begin[i] + 1;
			p[m++] = band_begin[i];

			for (j = band_begin[i]; j <= band_end[i]; j++)
			{
				if (j >= MAX_ALPHABET_SIZE)
				{
					AC_FREE(band_begin);
					AC_FREE(band_end);
					AC_FREE(full);
					AC_FREE(p);
					return -1;
				}

				p[m++] = full[j];
			}
		}

		next_state[k] = p;
	}

	AC_FREE(band_begin);
	AC_FREE(band_end);
	AC_FREE(full);
	return 0;
}


/*
 *  Create a new AC state machine
 */
ACSM_STRUCT2 * acsm_new2(void (*priv_data_free)(void *p))
{
	ACSM_STRUCT2 *p;

	init_xlatcase();
	p = AC_MALLOC(sizeof (ACSM_STRUCT2));

	if (p)
	{
		memset (p, 0, sizeof(ACSM_STRUCT2));

		/* Some defaults */
		p->fsa = FSA_DFA;
		p->format = ACF_FULL;
		p->alphabet_size = 256;
		p->sparse_max_row_nodes = 256;
		p->sparse_max_zcnt = 10;
		p->priv_data_free = priv_data_free;
	}

	return p;
}
/*
 *   Add a pattern to the list of patterns for this state machine
 *
 */
int acsm_add_pattern2(ACSM_STRUCT2 *p, unsigned char *pat, int n, int nocase,
		int offset, int depth, void *priv_data, int id)
{
	ACSM_PATTERN2 * plist;

	if (p == NULL)
		return -1;

	plist = AC_MALLOC(sizeof(ACSM_PATTERN2));
	if (plist == NULL)
		return -1;

	plist->patrn = AC_MALLOC(n);
	if (plist->patrn == NULL)
	{
		AC_FREE(plist);
		return -1;
	}

	memcpy(plist->patrn, pat, n);

	plist->n = n;
	plist->nocase = nocase;
	plist->offset = offset;
	plist->depth  = depth;
	plist->id    = id;
	plist->private = priv_data;

	plist->next = p->patterns;
	p->patterns = plist;
	p->num_patterns++;

	return 0;
}
/*
 *  Copy a boolean match flag int next_state table, for caching purposes.
 */
static void acsm_update_match_states(ACSM_STRUCT2 *acsm)
{
	acstate_t state;
	acstate_t **next_state = acsm->next_state;
	ACSM_PATTERN2 **match_list = acsm->match_list;

	for (state = 0; state < (acstate_t)acsm->num_states; state++)
	{
		acstate_t *p = next_state[state];

		if (match_list[state])
		{
			switch (acsm->sizeofstate)
			{
			case 1:
				*((u_int8_t *)p + 1) = 1;
				break;
			case 2:
				*((u_int16_t *)p + 1) = 1;
				break;
			default:
				p[1] = 1;
				break;
			}
		}
	}
}


/*
 *   Compile State Machine - NFA or DFA and Full or Banded or Sparse or SparseBands
 */
int acsm_compile2(ACSM_STRUCT2 *acsm)
{
	ACSM_PATTERN2 *plist;

	/* Count number of possible states */
	for (plist = acsm->patterns; plist != NULL; plist = plist->next)
		acsm->max_states += plist->n;

	acsm->max_states++; /* one extra */

	/* Alloc a List based State Transition table */
	acsm->trans_table = AC_MALLOC(sizeof(trans_node_t*) * acsm->max_states);
	if (acsm->trans_table == NULL)
	{
		acsm->max_states = 0;
		return -1;
	}

	acsm->match_list = AC_MALLOC(sizeof(ACSM_PATTERN2*) * acsm->max_states);
	if (acsm->match_list == NULL)
	{
		AC_FREE(acsm->trans_table);
		acsm->max_states = 0;
		return -1;
	}

	acsm->num_states = 0;

	for (plist = acsm->patterns; plist != NULL; plist = plist->next)
	{
		add_pattern_states(acsm, plist);
	}

	/* Add the 0'th state */
	acsm->num_states++;

	if (acsm->compress_states)
	{
		if (acsm->num_states < UCHAR_MAX)
		{
			acsm->sizeofstate = 1;
		}
		else if (acsm->num_states < USHRT_MAX)
		{
			acsm->sizeofstate = 2;
		}
		else
		{
			acsm->sizeofstate = 4;
		}
	}
	else
	{
		acsm->sizeofstate = 4;
	}

	/* Alloc a failure table - this has a failure state, 
	   and a match list for each state */
	acsm->fail_state = AC_MALLOC(sizeof(acstate_t) * acsm->num_states);
	if (acsm->fail_state == NULL)
	{
		goto err;
	}

	/* Alloc a separate state transition table == in state 's' 
	   due to event 'k', transition to 'next' state */
	acsm->next_state = AC_MALLOC(acsm->num_states * sizeof(acstate_t*));
	if (acsm->next_state == NULL)
	{
		goto err;
	}

	if ((acsm->fsa == FSA_DFA) || (acsm->fsa == FSA_NFA))
	{
		build_NFA(acsm);
	}

	if (acsm->fsa == FSA_DFA)
	{
		convert_NFA_to_DFA(acsm);
	}

	if (acsm->format == ACF_SPARSE)
	{
		if (conv_full_DFA_To_sparse(acsm))
			goto err;
	}
	else if (acsm->format == ACF_BANDED)
	{
		/* Convert DFA Full matrix to a Sparse matrix */
		if (conv_full_DFA_to_banded(acsm))
		{
			goto err;
		}
	}
	else if (acsm->format == ACF_SPARSEBANDS)
	{
		if (conv_full_DFA_to_sparse_bands(acsm))
			goto err;
	}
	else if ((acsm->format == ACF_FULL)
			|| (acsm->format == ACF_FULLQ))
	{
		if (conv_list_to_full(acsm))
			goto err;

		/* Don't need the fail_state table anymore */
		AC_FREE(acsm->fail_state);
	}

	acsm_update_match_states(acsm);

	list_free_trans_table(acsm);

	return 0;

err:
	AC_FREE(acsm->fail_state);
	if (acsm->match_list)
	{
		int i;
		ACSM_PATTERN2 *ilist, *mlist;

		for (i = 0; i < acsm->num_states; i++)
		{
			mlist = acsm->match_list[i];
			while (mlist)
			{
				ilist = mlist;
				mlist = mlist->next;
				AC_FREE(ilist);
			}

			if (acsm->next_state)
				AC_FREE(acsm->next_state[i]);
		}

		acsm->num_states = 0;
	}
	AC_FREE(acsm->match_list);
	AC_FREE(acsm->next_state);
	list_free_trans_table(acsm);
	acsm->max_states = 0;
	return -1;
}

/*
 *   Get the next_state from the NFA, all NFA storage formats use this
 */
static inline acstate_t sparse_get_next_state_NFA(acstate_t *ps, 
		acstate_t state, unsigned input)
{
	acstate_t fmt;
	acstate_t n;
	unsigned int index;
	int nb;

	fmt = *ps++;

	ps++;  /* skip bMatchState */

	switch( fmt )
	{
	case  ACF_BANDED:
	{
		n = ps[0];
		index = ps[1];

		if (input <  index)
		{
			if (state == 0)
			{
				return 0;
			}
			else
			{
				return (acstate_t)ACSM_FAIL_STATE2;
			}
		}
		if (input >= index + n)
		{
			if (state == 0)
			{
				return 0;
			}
			else
			{
				return (acstate_t)ACSM_FAIL_STATE2;
			}
		}
		if (ps[input-index] == 0)
		{
			if (state != 0)
			{
				return ACSM_FAIL_STATE2;
			}
		}

		return (acstate_t)ps[input - index];
	}

	case ACF_SPARSE:
	{
		n = *ps++; /* number of sparse index-value entries */

		for(; n>0 ; n--)
		{
			if (ps[0] > input)
			{
				return (acstate_t)ACSM_FAIL_STATE2;
			}
			else if (ps[0] == input)
			{
				return ps[1]; /* next state */
			}
			ps += 2;
		}
		if (state == 0)
		{
			return 0;
		}
		return ACSM_FAIL_STATE2;
	}

	case ACF_SPARSEBANDS:
	{
		nb = *ps++;   /* number of bands */

		while (nb > 0)  /* for each band */
		{
			n = *ps++;  /* number of elements */
			index = *ps++;  /* 1st element value */

			if (input <  index)
			{
				if (state != 0)
				{
					return (acstate_t)ACSM_FAIL_STATE2;
				}
				return (acstate_t)0;
			}
			if ((input >=  index) && (input < (index + n)))
			{
				if (ps[input-index] == 0)
				{
					if (state != 0)
					{
						return ACSM_FAIL_STATE2;
					}
				}
				return (acstate_t)ps[input - index];
			}
			nb--;
			ps += n;
		}
		if (state != 0)
		{
			return (acstate_t)ACSM_FAIL_STATE2;
		}
		return (acstate_t)0;
	}

	case ACF_FULL:
	case ACF_FULLQ:
	{
		if (ps[input] == 0)
		{
			if (state != 0)
			{
				return ACSM_FAIL_STATE2;
			}
		}
		return ps[input];
	}
	}

	return 0;
}



/*
 *   Get the next_state from the DFA Next State Transition table
 *   Full and banded are supported separately, this is for
 *   sparse and sparse-bands
 */
static inline acstate_t sparse_get_next_state_DFA(acstate_t *ps,
		acstate_t state, unsigned input)
{
	acstate_t n, nb;
	unsigned int index;

	switch (ps[0])
	{
	case  ACF_BANDED:
		if (input < ps[3])
			return 0;
		if (input >= (unsigned)(ps[3] + ps[2]))
			return 0;

		return  ps[4 + input - ps[3]];
	case ACF_FULL:
		return ps[2 + input];
	case ACF_SPARSE:
		n = ps[2]; /* number of entries/ key+next pairs */

		ps += 3;

		for (; n > 0; n--)
		{
			if (input < ps[0])
			{
				return (acstate_t)0;
			}
			else if (ps[0] == input)
			{
				return ps[1]; /* next state */
			}
			ps += 2;
		}
		return (acstate_t)0;
	case ACF_SPARSEBANDS:
		nb = ps[2]; /* number of bands */

		ps += 3;

		while (nb > 0)  /* for each band */
		{
			n = ps[0];  /* number of elements in this band */
			index = ps[1];  /* start index/char of this band */
			if (input <  index)
			{
				return (acstate_t)0;
			}
			if ((input < (index + n)))
			{
				return (acstate_t)ps[2 + input - index];
			}
			nb--;
			ps += 2 + n;
		}
		return (acstate_t)0;
	}

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
		if  (pattern_is_match_nocase(tx, index, pat, len) == 0)
			return 0;
	}

	return pattern_is_in_range(index, len, offset, depth);
}

/*
 *   Search Text or Binary Data for Pattern matches
 *
 *   Sparse & Sparse-Banded Matrix search
 */
static inline int acsm_search_sparse_DFA(ACSM_STRUCT2 *acsm, 
		unsigned char *text, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int* current_state )
{
	acstate_t state;
	ACSM_PATTERN2 *mlist;
	unsigned char *Tend;
	unsigned char *T, *Tc;
	int index;
	acstate_t **next_state = acsm->next_state;
	ACSM_PATTERN2 **match_list = acsm->match_list;

	Tc   = text;
	T    = text;
	Tend = T + n;

	if (!current_state)
	{
		return 0;
	}

	state = *current_state;

	for (; T < Tend; T++)
	{
		state = sparse_get_next_state_DFA(next_state[state], 
				state, xlatcase[*T]);

		/* test if this state has any matching patterns */
		if (next_state[state][1])
		{
			mlist = match_list[state];
			while (mlist)
			{
				index = T - mlist->n - Tc + 1;

				if (pattern_attr_is_match(text, index,
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
 *   Full format DFA search
 *   Do not change anything here without testing, caching and prefetching
 *   performance is very sensitive to any changes.
 *
 *   Perf-Notes:
 *    1) replaced ConvertCaseEx with inline xlatcase - this improves performance 5-10%
 *    2) using 'nocase' improves performance again by 10-15%, since memcmp is not needed
 *    3)
 */
#define AC_SEARCH \
	for( ; T < Tend; T++ ) { \
		ps = next_state[state]; \
		sindex = xlatcase[T[0]]; \
		if (ps[1]) \
		{ \
			mlist = match_list[state]; \
			while (mlist) \
			{ \
				index = T - mlist->n - text; \
				if (pattern_attr_is_match(text, index, \
							mlist->patrn, \
							mlist->n, \
							mlist->nocase, \
							mlist->offset, \
							mlist->depth) == 0) \
				{ \
					mlist = mlist->next; \
					continue; \
				} \
				if (match == NULL) { \
					*current_state = state; \
					return 1; \
				}\
				if (match(mlist->private, index, mlist->id, data) > 0) \
				{ \
					*current_state = state; \
					return 1; \
				} \
				mlist = mlist->next; \
			} \
		} \
		state = ps[2u + sindex]; \
	}

static inline int acsm_search_sparse_DFA_full(ACSM_STRUCT2 *acsm,
		unsigned char *text, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state)
{
	ACSM_PATTERN2 *mlist;
	unsigned char *Tend;
	unsigned char *T;
	int index;
	int sindex;
	acstate_t state;
	ACSM_PATTERN2 **match_list = acsm->match_list;

	T = text;
	Tend = T + n;

	if (current_state == NULL)
		return 0;

	state = *current_state;

	switch (acsm->sizeofstate)
	{
		case 1:
		{
			u_int8_t *ps;
			u_int8_t **next_state = (u_int8_t **)acsm->next_state;
			AC_SEARCH;
			break;
		}
		case 2:
		{
			u_int16_t *ps;
			u_int16_t **next_state = (u_int16_t **)acsm->next_state;
			AC_SEARCH;
			break;
		}
		default:
		{
			acstate_t *ps;
			acstate_t **next_state = acsm->next_state;
			AC_SEARCH;
			break;
		}
	}

	/* Check the last state for a pattern match */
	mlist = match_list[state];
	while (mlist)
	{
		index = T - mlist->n - text;

		if (pattern_attr_is_match(text, index,
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

	*current_state = state;
	return 0;
}

/*
 *   Banded-Row format DFA search
 *   Do not change anything here, caching and prefetching
 *   performance is very sensitive to any changes.
 *
 *   ps[0] = storage fmt
 *   ps[1] = bool match flag
 *   ps[2] = # elements in band
 *   ps[3] = index of 1st element
 */
static int acsm_search_sparse_DFA_banded(ACSM_STRUCT2 *acsm, 
		unsigned char *text, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state)
{
	acstate_t state;
	unsigned char *Tend;
	unsigned char *T;
	int sindex;
	int index;
	acstate_t **next_state = acsm->next_state;
	ACSM_PATTERN2 **match_list = acsm->match_list;
	ACSM_PATTERN2 *mlist;
	acstate_t *ps;

	T = text;
	Tend = T + n;

	if (!current_state)
	{
		return 0;
	}

	/* next must not be NULL, but i put this for safe */
	if (next_state == NULL)
	{
		return 0;
	}

	state = *current_state;
	if (state > acsm->num_states)
		state = 0;

	for (; T < Tend; T++)
	{
		ps = next_state[state];
		if (ps == NULL)
		{
			state = 0;
			ps = next_state[state];
		}

		sindex = xlatcase[T[0]];

		/* test if this state has any matching patterns */
		if (ps[1])
		{
			mlist = match_list[state];
			while (mlist)
			{
				index = T - mlist->n - text;

				if (pattern_attr_is_match(text, index,
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

		if ((acstate_t)sindex < ps[3])
			state = 0;
		else if((acstate_t)sindex >= (ps[3] + ps[2]))
			state = 0;
		else
			state = ps[4u + sindex - ps[3]];
	}

	/* Check the last state for a pattern match */
	mlist = match_list[state];
	while (mlist)
	{
		index = T - mlist->n - text;
		if (mlist->nocase == 0 && index >= 0)
		{
			if (memcmp(mlist->patrn, text + index, mlist->n))
			{
				mlist = mlist->next;
				continue;
			}
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

	*current_state = state;
	return 0;
}



/*
 *   Search Text or Binary Data for Pattern matches
 *
 *   Sparse Storage Version
 */
static inline int acsm_search_sparse_NFA(ACSM_STRUCT2 *acsm, 
		unsigned char *text, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int* current_state )
{
	acstate_t state;
	ACSM_PATTERN2 *mlist;
	unsigned char *Tend;
	unsigned char *T;
	int index;
	acstate_t **next_state = acsm->next_state;
	acstate_t *fail_state = acsm->fail_state;
	ACSM_PATTERN2 **match_list = acsm->match_list;
	unsigned char Tchar;

	T = text;
	Tend = T + n;

	if (!current_state)
	{
		return 0;
	}

	state = *current_state;

	for (; T < Tend; T++)
	{
		acstate_t nstate;

		Tchar = xlatcase[*T];

		nstate = sparse_get_next_state_NFA(next_state[state], 
				state, Tchar);
		while (nstate == ACSM_FAIL_STATE2)
		{
			state = fail_state[state];
			nstate = sparse_get_next_state_NFA(next_state[state], 
					state, Tchar);
		}

		state = nstate;

		mlist = match_list[state];
		while (mlist)
		{
			index = T - mlist->n - text;

			if (pattern_attr_is_match(text, index,
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

	*current_state = state;
	return 0;
}

/*
 *   Search Function
 */
int acsm_search2(ACSM_STRUCT2 *acsm, unsigned char *text, int n,
		int (*match)(void *priv_data, int index, int id, void *data),
		void *data, int *current_state)
{
	if (acsm == NULL)
		return 0;

	if (current_state == NULL)
		return 0;

	switch (acsm->fsa)
	{
	case FSA_DFA:
		if (acsm->format == ACF_FULL)
		{
			return acsm_search_sparse_DFA_full(acsm, text, n, 
					match, data, current_state);
		}
		else if (acsm->format == ACF_BANDED)
		{
			return acsm_search_sparse_DFA_banded(acsm, text, n, 
					match, data, current_state);
		}
		else
		{
			return acsm_search_sparse_DFA(acsm, text, n, 
					match, data, current_state);
		}

	case FSA_NFA:
		return acsm_search_sparse_NFA(acsm, text, n, 
				match, data, current_state );
	}

	return 0;
}


/*
 *   Free all memory
 */
void acsm_free2(ACSM_STRUCT2 *acsm)
{
	int i;
	ACSM_PATTERN2 *mlist, *ilist;

	if (acsm == NULL)
		return;

	for (i = 0; i < acsm->num_states; i++)
	{
		mlist = acsm->match_list[i];
		while (mlist)
		{
			ilist = mlist;
			mlist = mlist->next;
			AC_FREE(ilist);
		}

		AC_FREE(acsm->next_state[i]);
	}

	for (mlist = acsm->patterns; mlist;)
	{
		ilist = mlist;
		mlist = mlist->next;

		if (acsm->priv_data_free && (ilist->private != NULL))
			acsm->priv_data_free(ilist->private);

		AC_FREE(ilist->patrn);
		AC_FREE(ilist);
	}

	AC_FREE(acsm->next_state);
	AC_FREE(acsm->fail_state);
	AC_FREE(acsm->match_list);
	AC_FREE(acsm);
}

int acsm_pattern_count2(ACSM_STRUCT2 * acsm)
{
	if (acsm == NULL)
		return 0;

	return acsm->num_patterns;
}

int acsm_select_format2(ACSM_STRUCT2 *acsm, int m)
{
	switch (m)
	{
		case ACF_FULL:
		case ACF_SPARSE:
		case ACF_BANDED:
		case ACF_SPARSEBANDS:
			acsm->format = m;
			break;
		default:
			return -1;
	}

	return 0;
}

