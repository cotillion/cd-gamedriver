#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "interpret.h"
#include "object.h"
#include "regexp.h"
#include "mapping.h"
#include "exec.h"
#include "main.h"
#include "simulate.h"
#include "comm1.h"
#include "backend.h"

#include "inline_eqs.h"
#include "inline_svalue.h"

extern struct svalue *sp;

/*
 * This file contains functions used to manipulate arrays.
 * Some of them are connected to efuns, and some are only used internally
 * by the game driver.
 */
extern int d_flag;

int num_arrays;
int total_array_size;

/*
 * Make an empty vector for everyone to use, never to be deallocated.
 * It is cheaper to reuse it, than to use malloc() and allocate.
 */
struct vector null_vector = {
    0,	/* size */
    1	/* Ref count, which will ensure that it will never be deallocated */
};

/*
 * Allocate an array of size 'n'.
 */
struct vector *
allocate_array(long long nn)
{
    int i, n = nn;
    struct vector *p;

    if (nn < 0 || nn > MAX_ARRAY_SIZE)
	error("Illegal array size.\n");
    if (n == 0) {
        p = &null_vector;
	INCREF(p->ref);
	return p;
    }
    num_arrays++;
    total_array_size += sizeof (struct vector) + sizeof (struct svalue) *
	(n-1);
    p = ALLOC_VECTOR(n);
    p->ref = 1;
    p->size = n;
    for (i=0; i<n; i++)
	p->item[i] = const0;
    return p;
}

void 
free_vector(struct vector *p)
{
    int i;
    
    if (!p->ref || --p->ref > 0)
	return;
#if 0
    if (p->ref < 0) {
	debug_message("Array reference count < 0 in free_vector.\n");
	return;
    }
#endif
#if defined(DEBUG)
    if (p == &null_vector)
    {
	p->ref = 1;
	debug_message("Tried to free the zero-size shared vector.\n");
	return;
    }
#endif
    for (i = 0; i < p->size; i++)
	free_svalue(&p->item[i]);
    num_arrays--;
    total_array_size -= sizeof (struct vector) + sizeof (struct svalue) *
	(p->size-1);
    free((char *)p);
}

struct vector *
multiply_array(struct vector *vec, long long factor)
{
    struct vector *result;
    long long size = vec->size, newsize,j, offset;

    if (factor <= 0 || size == 0) {
	return allocate_array(0);
    }

    if (factor > MAX_ARRAY_SIZE)
	error("Illegal array size.\n"); 

    newsize = size * factor;
    result = allocate_array(newsize);
    for (offset = 0; offset < newsize;) {
	for (j = 0; j < size; j++, offset++)
	    assign_svalue_no_free(result->item + offset, vec->item + j);
    }
    return result;
}

struct vector *
explode_string(char *str, char *del)
{
    char *p, *beg;
    int num, extra;
    struct vector *ret;
    size_t len;
    char *buff;

    len = strlen(del);
    /*
     * Take care of the case where the delimiter is an
     * empty string. Then, return an array with only one element,
     * which is the original string.
     */
    if (len == 0)
    {
	len = strlen(str); 
	ret = allocate_array((int)len);
	for (num = 0; num < len; num++)
	{
	    ret->item[num].type = T_STRING;
	    ret->item[num].string_type = STRING_MSTRING;
	    ret->item[num].u.string = allocate_mstring(1);
	    ret->item[num].u.string[0] = str[num];
	    ret->item[num].u.string[1] = '\0';
	}
	return ret;
    }

    if (!*str) /* Empty string */
      return allocate_array(0);

#ifdef OLD_EXPLODE
    /*
     * Skip leading 'del' strings, if any.
     */
    while(strncmp(str, del, len) == 0) 
    {
      str += len;
      if (str[0] == '\0')
          return allocate_array(0);
    }
#endif
    /*
     * Find number of occurences of the delimiter 'del'.
     */
    extra = 1;
    num = 0;
    for (p = str; *p;) 
    {
	if (strncmp(p, del, len) == 0) 
	{
	    num++;
	    p += len;
	    extra = 0;
	} 
	else
	{
	    p += 1;
	    extra = 1;
	}
    }
    /*
     * Compute number of array items. It is either number of delimiters,
     * or, one more.
     */
#ifndef KINGDOMS_EXPLODE
    if (extra)
#endif
	num++;
    buff = alloca(strlen(str) + 1);
    ret = allocate_array(num);
    beg = str;
    num = 0;
    for (p = str; *p; ) 
    {
	if (strncmp(p, del, len) == 0) 
	{
	    (void)strncpy(buff, beg, (size_t)(p - beg));
	    buff[p-beg] = '\0';
	    if (num >= ret->size)
		fatal("Too big index in explode !\n");
	    /* free_svalue(&ret->item[num]); Not needed for new array */
	    ret->item[num].type = T_STRING;
	    ret->item[num].string_type = STRING_MSTRING;
	    ret->item[num].u.string = make_mstring(buff);
	    num++;
	    beg = p + len;
	    p = beg;
	} 
	else 
	{
	    p += 1;
	}
    }
    /* Copy last occurence, if there was not a 'del' at the end. */
#ifndef KINGDOMS_EXPLODE
    if (*beg != '\0')
#endif
    {
	/* free_svalue(&ret->item[num]); Not needed for new array */
	if (num >= ret->size)
	    fatal("Too big index in explode !\n");
	ret->item[num].type = T_STRING;
	ret->item[num].string_type = STRING_MSTRING;
	ret->item[num].u.string = make_mstring(beg);
    }
    return ret;
}

char *
implode_string(struct vector *arr, char *del)
{
    size_t size, len;
    int i, num;
    char *p, *ret;

    size = 0;
    num = 0;
    for (i=0; i < arr->size; i++) 
    {
	if (arr->item[i].type == T_STRING)
	{
	    size += strlen(arr->item[i].u.string);
	    num++;
	}
    }
    if (num == 0)
	return make_mstring("");
    
    len = strlen(del);
    ret = allocate_mstring(size + (num-1) * len);
    p = ret;
    p[0] = '\0';
    size = 0;
    num = 0;
    for (i = 0; i < arr->size; i++) 
    {
	if (arr->item[i].type == T_STRING) 
	{
	    if (num > 0) 
	    {
		(void)strcpy(p, del);
		p += len;
	    }
	    (void)strcpy(p, arr->item[i].u.string);
	    p += strlen(arr->item[i].u.string);
	    num++;
	}
    }
    return ret;
}

struct vector *
users() 
{
    int i;
    struct vector *ret;
    
    if (!num_player)
	return allocate_array(0);
    
    ret = allocate_array(num_player);
    for (i = 0; i < num_player; i++) {
	ret->item[i].type = T_OBJECT;
	add_ref(ret->item[i].u.ob = get_interactive_object(i),"users");
    }
    return ret;
}

/*
 * Slice of an array.
 */
struct vector *
slice_array(struct vector *p, long long from, long long to)
{
    struct vector *d;
    long long cnt;

#ifdef NEGATIVE_SLICE_INDEX    
    if (from < 0)
	from = p->size + from;
#endif
    if (from < 0)
	from = 0;
    if (from >= p->size)
	return allocate_array(0); /* Slice starts above array */
#ifdef NEGATIVE_SLICE_INDEX
    if (to < 0)
	to = p->size + to;
#endif
    if (to >= p->size)
	to = p->size - 1;
    if (to < from)
	return allocate_array(0); 
    
    d = allocate_array(to - from + 1);
    for (cnt = from; cnt <= to; cnt++) 
	assign_svalue_no_free (&d->item[cnt - from], &p->item[cnt]);
    
    return d;
}

/* EFUN: filter (array part)
   
   Runs all elements of an array through fun
   and returns an array holding those elements that fun
   returned 1 for.
   */
struct vector *
filter_arr(struct vector *p, struct closure *fun)
{
    struct vector *r;
    char *flags;
    int cnt,res;
    
    if (p->size<1)
	return allocate_array(0);

    res = 0;
    flags = tmpalloc((size_t)p->size + 1); 
    for (cnt = 0; cnt < p->size; cnt++) {
	push_svalue(&p->item[cnt]);
	(void)call_var(1, fun);
	if (sp->type == T_NUMBER && sp->u.number) {
	    flags[cnt] = 1; 
	    res++; 
	} else
	    flags[cnt] = 0;
	pop_stack();
    }
    r = allocate_array(res);
    for (cnt = res = 0; res < r->size && cnt < p->size; cnt++) {
	if (flags[cnt]) 
	    assign_svalue_no_free(&r->item[res++], &p->item[cnt]);
    }
/*    tmpfree(flags); */
    return r;
}

/* Unique maker
   
   These routines takes an array of objects and calls the function 'func'
   in them. The return values are used to decide which of the objects are
   unique. Then an array on the below form are returned:
   
   ({
   ({Same1:1, Same1:2, Same1:3, .... Same1:N }),
   ({Same2:1, Same2:2, Same2:3, .... Same2:N }),
   ({Same3:1, Same3:2, Same3:3, .... Same3:N }),
   ....
   ....
   ({SameM:1, SameM:2, SameM:3, .... SameM:N }),
   })
   i.e an array of arrays consisting of lists of objectpointers
   to all the nonunique objects for each unique set of objects.
   
   The basic purpose of this routine is to speed up the preparing of the
   array used for describing.
   
   */

struct unique
{
    int count;
    struct svalue *val;
    struct svalue mark;
    struct unique *same;
    struct unique *next;
};

static int 
put_in(struct unique **ulist, struct svalue *marker, struct svalue *elem)
{
    struct unique *llink, *slink, *tlink;
    int cnt,fixed;
    
    llink = *ulist;
    cnt = 0; fixed = 0;
    while (llink) 
    {
	if ((!fixed) && (equal_svalue(marker, &(llink->mark))))
	{
	    for (tlink = llink; tlink->same; tlink = tlink->same) 
		(tlink->count)++;
	    (tlink->count)++;
	    slink = (struct unique *) tmpalloc(sizeof(struct unique));
	    slink->count = 1;
	    assign_svalue_no_free(&slink->mark, marker);
	    slink->val = elem;
	    slink->same = 0;
	    slink->next = 0;
	    tlink->same = slink;
	    fixed = 1; /* We want the size of the list so do not break here */
	}
	llink = llink->next;
	cnt++;
    }
    if (fixed) 
	return cnt;
    llink = (struct unique *) tmpalloc(sizeof(struct unique));
    llink->count = 1;
    assign_svalue_no_free(&llink->mark, marker);
    llink->val = elem;
    llink->same = 0;
    llink->next = *ulist;
    *ulist = llink;
    return cnt + 1;
}


struct vector *
make_unique(struct vector *arr, struct closure *fun, struct svalue *skipnum)
{
    struct vector *res, *ret;
    struct unique *head, *nxt, *nxt2;
    
    int cnt, ant, cnt2;
    
    if (arr->size < 1)
	return allocate_array(0);

    head = 0; 
    ant = 0; 
    INCREF(arr->ref);
    for(cnt = 0; cnt < arr->size; cnt++) 
    {
	if (arr->item[cnt].type == T_OBJECT)
	{
            push_svalue(&arr->item[cnt]);
            (void)call_var(1, fun);

	    if ((!sp) || (sp->type != skipnum->type) || !equal_svalue(sp, skipnum)) 
	    {
		if (sp) 
		{
		    ant = put_in(&head, sp, &(arr->item[cnt]));
		}
	    }

            pop_stack();
	}
    }
    DECREF(arr->ref);
    ret = allocate_array(ant);
    
    for (cnt = ant - 1; cnt >= 0; cnt--) /* Reverse to compensate put_in */
    {
	ret->item[cnt].type = T_POINTER;
	ret->item[cnt].u.vec = res = allocate_array(head->count);
	nxt2 = head;
	head = head->next;
	cnt2 = 0;
	while (nxt2) 
	{
	    assign_svalue_no_free (&res->item[cnt2++], nxt2->val);
	    free_svalue(&nxt2->mark);
	    nxt = nxt2->same;
/*	    tmpfree((char *) nxt2); */
	    nxt2 = nxt;
	}
	if (!head) 
	    break; /* It shouldn't but, to avoid skydive just in case */
    }
    return ret;
}

/*
 * End of Unique maker
 *************************
 */

/* Concatenation of two arrays into one
 */
struct vector *
add_array(struct vector *p, struct vector *r)
{
    int cnt,res;
    struct vector *d;
    
    d = allocate_array(p->size + r->size);
    res = 0;
    for (cnt = 0; cnt < p->size; cnt++) 
    {
	assign_svalue_no_free (&d->item[res],&p->item[cnt]);
	res++; 
    }
    for (cnt = 0; cnt < r->size; cnt++)
    {
	assign_svalue_no_free (&d->item[res],&r->item[cnt]);
	res++; 
    }
    return d;
}


/* Returns an array of all objects contained in 'ob'
 */
struct vector *
all_inventory(struct object *ob)
{
    struct vector *d;
    struct object *cur;
    int cnt,res;
    
    cnt = 0;
    for (cur = ob->contains; cur; cur = cur->next_inv)
	cnt++;
    
    if (!cnt)
	return allocate_array(0);

    d = allocate_array(cnt);
    cur = ob->contains;
    
    for (res = 0; res < cnt; res++) 
    {
	d->item[res].type = T_OBJECT;
	d->item[res].u.ob = cur;
	add_ref(cur, "all_inventory");
	cur = cur->next_inv;
    }
    return d;
}


/* Runs all elements of an array through fun
   and replaces each value in arr by the value returned by fun
   */
struct vector *
map_array (struct vector *arr, struct closure *fun)
{
    struct vector *r;
    int cnt;

    r = allocate_array(arr->size);
    push_vector(r, 0);
    for (cnt = 0; cnt < arr->size; cnt++) {
	push_svalue(&arr->item[cnt]);
	(void)call_var(1, fun);
	r->item[cnt] = *sp;	/* Just copy it.  Reference count is correct */
	sp--;			/* since we loose a reference here. */
    }
    sp--;
    return r;
}

/*
 * deep_inventory()
 *
 * This function returns the recursive inventory of an object. The returned 
 * array of objects is flat, ie there is no structure reflecting the 
 * internal containment relations.
 *
 */
struct vector *
deep_inventory(struct object *ob, int take_top)
{
    struct vector	*dinv, *ainv, *sinv, *tinv;
    int			il;

    ainv = all_inventory(ob);
    if (take_top)
    {
	sinv = allocate_array(1);
	sinv->item[0].type = T_OBJECT;
	add_ref(ob,"deep_inventory");
	sinv->item[0].u.ob = ob;
	dinv = add_array(sinv, ainv);
	free_vector(sinv);
	free_vector(ainv);
	ainv = dinv;
    }
    sinv = ainv;
    for (il = take_top ? 1 : 0 ; il < ainv->size ; il++)
    {
	if (ainv->item[il].u.ob->contains)
	{
	    dinv = deep_inventory(ainv->item[il].u.ob,0);
	    tinv = add_array(sinv, dinv);
	    if (sinv != ainv)
		free_vector(sinv);
	    sinv = tinv;
	    free_vector(dinv);
	}
    }
    if (ainv != sinv)
	free_vector(ainv);
    return sinv;
}

struct vector *
match_regexp(struct vector *v, char *pattern)
{
    struct regexp *reg;
    char *res;
    int i, num_match;
    struct vector *ret;

    if (v->size == 0)
	return allocate_array(0);
    reg = regcomp(pattern, 0);
    if (reg == 0)
	return 0;
    res = (char *)alloca((size_t)v->size);
    for (num_match=i=0; i < v->size; i++)
    {
	res[i] = 0;
	if (v->item[i].type != T_STRING)
	    continue;
	eval_cost++;
	if (regexec(reg, v->item[i].u.string) == 0)
	    continue;
	res[i] = 1;
	num_match++;
    }
    ret = allocate_array(num_match);
    for (num_match=i=0; i < v->size; i++)
    {
	if (res[i] == 0)
	    continue;
	assign_svalue_no_free(&ret->item[num_match], &v->item[i]);
	num_match++;
    }
    free((char *)reg);
    return ret;
}

/* 
    An attempt at rewrite using mappings
*/
#define SETARR_SUBTRACT 	1
#define SETARR_INTERSECT 	2

struct vector *
set_manipulate_array(struct vector *arr1, struct vector *arr2, int op)
{
    struct mapping *m;
    struct vector *r;
    struct svalue tmp, *v;
    char *flags;
    int cnt, res;
    
    if (arr1->size < 1 || arr2->size < 1)
    {
	switch (op)
	{
	case SETARR_SUBTRACT:
	    INCREF(arr1->ref);
	    return arr1;

        case SETARR_INTERSECT:
	    return allocate_array(0);
	}
    }

    m = make_mapping(arr2, 0);
    tmp.type = T_NUMBER;
    tmp.u.number = 1;
    assign_svalue_no_free(get_map_lvalue(m, &arr2->item[0], 1), &tmp);

    res = 0;

    flags = alloca((size_t)arr1->size + 1); 
    for (cnt = 0; cnt < arr1->size; cnt++) 
    {
	flags[cnt] = 0;
	v = get_map_lvalue(m, &(arr1->item[cnt]), 0);
	if (op == SETARR_INTERSECT && v != &const0)
	{
	    flags[cnt] = 1; 
	    res++; 
	}
	else if (op == SETARR_SUBTRACT && v == &const0)
	{
	    flags[cnt] = 1; 
	    res++; 
	}
    }
    r = allocate_array(res);
    if (res) 
    {
	for (cnt = res = 0; cnt < arr1->size; cnt++) 
	{
	    if (flags[cnt]) 
		assign_svalue_no_free(&r->item[res++], &arr1->item[cnt]);
	}
    }
    free_mapping(m);
    return r;
}

INLINE struct vector *
intersect_array(struct vector *arr1, struct vector *arr2)
{
    return set_manipulate_array(arr1, arr2, SETARR_INTERSECT);
}

INLINE struct vector *
subtract_array(struct vector *arr1, struct vector *arr2)
{
    return set_manipulate_array(arr1, arr2, SETARR_SUBTRACT);
}

INLINE struct vector *
union_array(struct vector *arr1, struct vector *arr2)
{
    int i, size;
    struct mapping *mp;
    struct vector *arr3;
    char *set;

    if (arr1->size == 0)
    {
	INCREF(arr2->ref);
	return arr2;
    }

    if (arr2->size == 0)
    {
	INCREF(arr1->ref);
	return arr1;
    }

    mp = allocate_map(arr1->size);

    for (i = 0; i < arr1->size; i++)
	assign_svalue(get_map_lvalue(mp, &arr1->item[i], 1), &const1);

    set = alloca((size_t)arr2->size);

    for (i = size = 0; i < arr2->size; i++)
    {
	if (get_map_lvalue(mp, &arr2->item[i], 0) == &const0)
	    set[i] = 1, size++;
	else
	    set[i] = 0;
    }

    free_mapping(mp);

    arr3 = allocate_array(arr1->size + size);

    for (i = 0; i < arr1->size; i++)
	assign_svalue_no_free(&arr3->item[i], &arr1->item[i]);

    size = arr1->size;

    for (i = 0; i < arr2->size; i++)
	if (set[i])
	    assign_svalue_no_free(&arr3->item[size++], &arr2->item[i]);

    return arr3;
}
