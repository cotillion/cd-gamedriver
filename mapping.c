#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>		/* sys/types.h and netinet/in.h are here to enable include of comm.h below */
#include <sys/stat.h>
/* #include <netinet/in.h> Included in comm.h below */
#include <memory.h>
#include <stdio.h>

#include "config.h"
#include "lint.h"
#include "exec.h"
#include "interpret.h"
#include "object.h"
#include "instrs.h"
#include "comm.h"
#include "mapping.h"
#include "hash.h"
#include "simulate.h"
#include "mstring.h"

#include "inline_eqs.h"
#include "inline_svalue.h"

static void rehash_map(struct mapping *);

/* rehash when the mapping is filled to this many % */
#define FILL_FACTOR 250
#define FSIZE(s) (((s)*FILL_FACTOR)/100)
int num_mappings = 0, total_mapping_size = 0;

#if 0
void
abortmap(char *s)
{
    (void)fprintf(stderr, "Mapping %s\n", s);
    error("Mappings not implemented");
}
#endif

int
free_apairs(struct apair *p)
{
    struct apair *next;
    int pairs = 0;
    
    for (;p;p = next) {
	free_svalue(&p->arg);
	free_svalue(&p->val);
	next = p->next;
	free((char *)p);
	total_mapping_size -= sizeof(struct apair);
	pairs++;
    }
    return pairs;
}

void
free_mapping(struct mapping *m)
{
    short i;

    if (!m->ref || --m->ref > 0)
	return;
    for (i = 0; i < m->size; i++)
	m->card -= free_apairs(m->pairs[i]);
    num_mappings--;
    total_mapping_size -= (sizeof(struct apair *) * m->size + 
			   sizeof(struct mapping));
    free((char *)m->pairs);
    free((char *)m);
}

struct mapping *
allocate_map(short msize)
{
    struct mapping *m;
    unsigned u;
    short size;

    for(size = 1, u = (msize*100)/FILL_FACTOR; u && size < MAX_MAPPING_SIZE; size <<= 1, u >>= 1)
	;

    num_mappings++;
    m = (struct mapping *) xalloc(sizeof(struct mapping));
    m->pairs = (struct apair **) xalloc(sizeof(struct apair *) * size);
    (void)memset(m->pairs, 0, sizeof(struct apair *) * size);
    total_mapping_size += sizeof(struct mapping) + sizeof(struct apair *) * size;
    m->ref = 1;
    m->card = 0;
    m->size = size;
    m->mcard = FSIZE(m->size);

    return m;
}

short
card_mapping(struct mapping *m)
{
    return m->card;
}

static INLINE struct apair *
newpair(struct apair *n, struct svalue *k, struct svalue *v, short h)
{
    struct apair *p = (struct apair *)xalloc(sizeof(struct apair));

    p->next = n;
    p->hashval = h;
    assign_svalue_no_free(&p->arg, k);
    assign_svalue_no_free(&p->val, v);
    total_mapping_size += sizeof(struct apair);
    return p;
}

static INLINE short
hashsvalue(struct svalue *v)
{
    switch(v->type) {
    case T_NUMBER:
	return (unsigned short)v->u.number;
    case T_POINTER:
	return (unsigned short)((unsigned long)v->u.vec >> 4);
    case T_MAPPING:
	return (unsigned short)((unsigned long)v->u.map >> 4);
    case T_STRING:
	return hashstr16(v->u.string, 20);
    case T_OBJECT:
	return (unsigned short)((unsigned long)v->u.ob >> 4);
    case T_FLOAT:
	return (unsigned short)v->u.number;
    default:
        return 0;
    }
}

struct svalue *
get_map_lvalue(struct mapping *m, struct svalue *k, int c)
{
    short h, h16;
    struct apair *p;

    h16 = hashsvalue(k);
    h = h16 & (m->size-1);

    for(p = m->pairs[h]; p; p = p->next) {
	if (p->hashval == h16 && equal_svalue(k, &p->arg))
	    break;
    }
    if (!p) {
	if (c) {
	    m->pairs[h] = p = newpair(m->pairs[h], k, &const0, h16);
	    if (++m->card > m->mcard) {
		/* We need to extend the hash table */
		rehash_map(m);
	    }
	} else {
	    /* Return address of a dummy location, with 0. */
	    return &const0;
	}
    }
    return &p->val;
}

struct vector *
map_domain(m)
struct mapping *m;
{
    struct vector *d;
    short cnt, i, k;
    struct apair *p;
    
    cnt = m->card;
    d = allocate_array(cnt);
    for (k = 0, i = 0; i < m->size; i++)
	for(p = m->pairs[i]; p; p = p->next)
	    assign_svalue_no_free (&d->item[k++], &p->arg);
    return d;
}

struct vector *
map_codomain(struct mapping *m)
{
    struct vector *d;
    short cnt, i, k;
    struct apair *p;
    
    cnt = m->card;
    d = allocate_array(cnt);
    for (k = 0, i = 0; i < m->size; i++)
	for(p = m->pairs[i]; p; p = p->next)
	    assign_svalue_no_free(&d->item[k++], &p->val);
    return d;
}

struct mapping *
make_mapping(struct vector *ind, struct vector *val)
{
    struct mapping *m;
    struct svalue tmp;
    short i, max;

    tmp.type = T_NUMBER;
#ifdef PURIFY
    tmp.string_type = -1;
#endif

    if (ind != NULL && val != NULL) {
	max = ind->size < val->size ? ind->size : val->size;
	m = allocate_map(max);
	for (i = 0 ; i < max ; i++)
	{
	    assign_svalue(get_map_lvalue(m, &ind->item[i], 1), 
				  &val->item[i]);
	}
    } else if (ind != NULL && val == NULL) {
	m = allocate_map(ind->size);
	for (i = 0 ; i < ind->size ; i++)
	{
	    tmp.u.number = i;
	    assign_svalue(get_map_lvalue(m, &ind->item[i], 1), &tmp);
	}
    } else if (ind == NULL && val != NULL) {
	m = allocate_map(val->size);
	for (i = 0 ; i < val->size ; i++)
	{
	    tmp.u.number = i;
	    assign_svalue_no_free(get_map_lvalue(m, &tmp, 1), &val->item[i]);
	}
    } else {
	m = allocate_map(0);
    }

    return m;
}

struct mapping *
add_mapping(struct mapping *m1, struct mapping *m2)
{
    struct mapping *retm;
    struct apair *p;
    short i;

    retm = allocate_map((short)(m1->card + m2->card));

    for (i = 0 ; i < m1->size ; i++)
    {
	for (p = m1->pairs[i]; p ; p = p->next)
	    assign_svalue(get_map_lvalue(retm, &p->arg, 1), &p->val);
    }
    for (i = 0 ; i < m2->size ; i++)
    {
	for (p = m2->pairs[i]; p ; p = p->next)
	    assign_svalue(get_map_lvalue(retm, &p->arg, 1), &p->val);
    }
    return retm;
}

void
addto_mapping(struct mapping *m1, struct mapping *m2)
{
    struct apair *p;
    short i;

    for (i = 0 ; i < m2->size ; i++)
    {
	for (p = m2->pairs[i]; p ; p = p->next)
	    assign_svalue(get_map_lvalue(m1, &p->arg, 1), &p->val);
    }
}

void
remove_from_mapping(struct mapping *m, struct svalue *val)
{
    struct apair **p;
    short h;

    h = hashsvalue(val) & (m->size-1);

    for (p = &m->pairs[h]; *p; p = &(*p)->next)
      if (equal_svalue(val, &(*p)->arg)) {
	struct apair *del = *p;
	*p = del->next;
	del->next = NULL;
	m->card--;
	free_apairs(del);
	return;
      }
    return;

}

struct mapping *
remove_mapping(struct mapping *m, struct svalue *val)
{
    struct mapping *retm;
    struct apair *p;
    short i, h;

    retm = allocate_map(m->size);
    h = hashsvalue(val) & (m->size-1);

    for (i = 0 ; i < m->size ; i++)
    {
	for (p = m->pairs[i] ; p ; p = p->next)
	{
	    if (h == i)
	    {
		if (!equal_svalue(val, &p->arg))
		    assign_svalue(get_map_lvalue(retm, &p->arg, 1), &p->val);
	    }
	    else
		assign_svalue(get_map_lvalue(retm, &p->arg, 1), &p->val);
	}
    }
    return retm;
}

static void
rehash_map(struct mapping *map)
{
    register struct apair **pairs, *next, *ptr;
    short i, hval;
    unsigned int nsize;

    nsize = map->size * 2;
    if (nsize > MAX_MAPPING_SIZE) {
	error("Warning, too large mapping.\n");
	return;
    }

    pairs = (struct apair **)xalloc(sizeof(struct apair *) * nsize);
    (void)memset(pairs, 0, sizeof(struct apair *) * nsize);

    for (i = 0 ; i < map->size ; i++) {
	for (ptr = map->pairs[i]; ptr; ptr = next) {
	    hval = ptr->hashval & (nsize-1);
	    next = ptr->next;
	    ptr->next = pairs[hval];
	    pairs[hval] = ptr;
	}
    }

    free((char *)map->pairs);
    map->pairs = pairs;
    total_mapping_size += sizeof(struct apair *) * (nsize - map->size);
    map->size = nsize;
    map->mcard = FSIZE(map->size);
}


struct mapping *
copy_mapping(struct mapping *m)
{
    struct mapping *cm;
    struct apair *pair;
    short i;
    
    num_mappings++;
    cm = (struct mapping *) xalloc(sizeof(struct mapping));
    cm->pairs = (struct apair **) xalloc(sizeof(struct apair *) * m->size);
    (void)memset(cm->pairs, 0, sizeof(struct apair *) * m->size);
    total_mapping_size += sizeof(struct mapping) + sizeof(struct apair *) * m->size;
    cm->ref = 1;
    cm->size = m->size;
    cm->card = m->card;
    cm->mcard = m->mcard;
    for (i = 0; i < m->size; i++)
	for(pair = m->pairs[i]; pair; pair = pair->next)
	    cm->pairs[i] = newpair(cm->pairs[i], &pair->arg, &pair->val, pair->hashval);
    return cm;
}

/* Runs all codomain elements of a mapping through fun
   and replaces each value in the codomain map by the value returned 
   by fun.
   */
struct mapping *
map_map(struct mapping *map, struct closure *fun)
{
    extern void push_mapping(struct mapping *, bool_t);
    struct mapping *r;
    struct apair *p;
    short i;

    r = copy_mapping(map);	/* copy entire mapping */
    push_mapping(r, 0);
    for (i = 0 ; i < r->size ; i++) {
	for(p = r->pairs[i]; p; p = p->next) {
	    push_svalue(&p->val);
	    (void)call_var(1, fun);
	    assign_svalue(&p->val, sp);	/* replace old value */
	    pop_stack();	/* and pop it */
	}
    }
    sp--;
    return r;
}


/* EFUN: filter (mapping part)
   
   Runs all codomaing elements of an map through fun
   and returns a mapping holding those elements that fun
   returned non-zero for.
   */
struct mapping *
filter_map(struct mapping *map, struct closure *fun)
{
    struct mapping *r;
    struct apair **p;
    short i;

    r = copy_mapping(map);
    for (i = 0; i < r->size; i++) {
	for(p = &r->pairs[i]; *p;) {
	    push_svalue(&(*p)->val);
	    (void)call_var(1, fun);
	    if (sp->type == T_NUMBER && sp->u.number == 0) {
		/* remove it from the list */
		struct apair *ap = *p;
		*p = (*p)->next;	/* remove element, but don't move p */
		free_svalue(&ap->arg);
		free_svalue(&ap->val);
		free(ap);
		total_mapping_size -= sizeof(struct apair);
		r->card--;
	    } else {
		p = &(*p)->next; /* step to next */
	    }
	    pop_stack();
	}
    }
    return r;
}


