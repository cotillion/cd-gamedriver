#include <stdio.h>
#include <string.h>

#include "config.h"
#include "lint.h"
#include "interpret.h"
#include "object.h"
#include "hash.h"
#include "simulate.h"
#include "exec.h"
/*
 * Object name hash table.  Object names are unique, so no special
 * problems - like stralloc.c.  For non-unique hashed names, we need
 * a better package (if we want to be able to get at them all) - we
 * cant move them to the head of the hash chain, for example.
 *
 * Note: if you change an object name, you must remove it and reenter it.
 */

/*
 * hash table - list of pointers to heads of object chains.
 * Each object in chain has a pointer, next_hash, to the next object.
 * OTABLE_SIZE is in config.h, and should be a prime, probably between
 * 100 and 1000.  You can have a quite small table and still get very good
 * performance!  Our database is 8Meg; we use about 500.
 */

static struct object *(obj_table[OTABLE_SIZE]);

/*
 * Object hash function, ripped off from stralloc.c.
 */


#if BITNUM(OTABLE_SIZE) == 1
/* This one only works for even power-of-2 table size, but is faster */
#define ObjHash(s) (hashstr16((s), 100) & ((OTABLE_SIZE)-1))
#else
#define ObjHash(s) (hashstr((s), 100, OTABLE_SIZE))
#endif

/*
 * Looks for obj in table, moves it to head.
 */

static long long obj_searches = 0, obj_probes = 0, objs_found = 0;

static struct object * 
find_obj_n(char *s)
{
    struct object * curr, *prev;
    
    int h = ObjHash(s);
    
    curr = obj_table[h];
    prev = 0;
    
    obj_searches++;
    
    while (curr)
    {
	obj_probes++;
	if (strcmp(curr->name, s) == 0) { /* found it */
	    if (prev)
	    { /* not at head of list */
		prev->next_hash = curr->next_hash;
		curr->next_hash = obj_table[h];
		obj_table[h] = curr;
	    }
	    objs_found++;
	    return(curr);	/* pointer to object */
	}
	prev = curr;
	curr = curr->next_hash;
    }
    
    return(0); /* not found */
}

/*
 * Add an object to the table - can't have duplicate names.
 */

static int objs_in_table = 0;

void 
enter_object_hash(struct object *ob)
{
    struct object *s, *sibling;
    int h = ObjHash(ob->name);

   /* Add to object list */
    if (ob->flags & O_CLONE && ob->prog->clones) {
	sibling = ob->prog->clones;
	ob->next_all = sibling;
	ob->prev_all = sibling->prev_all;
	sibling->prev_all->next_all = ob;
	sibling->prev_all = ob;
	if (sibling == obj_list)
	    obj_list = ob;
    } else if (obj_list) {
	ob->next_all = obj_list;
	ob->prev_all = obj_list->prev_all;
	obj_list->prev_all->next_all = ob;
	obj_list->prev_all = ob;
	obj_list = ob;
    }
    else
	obj_list = ob->next_all = ob->prev_all = ob;

    if (ob->flags & O_CLONE) {
	ob->prog->clones = ob;
	ob->prog->num_clones++;
    }

    s = find_obj_n(ob->name);
    if (s) {
	if (s != ob)
	    fatal("Duplicate object \"%s\" in object hash table\n",
		  ob->name);
	else
	    fatal("Entering object \"%s\" twice in object table\n",
		  ob->name);
    }
    if (ob->next_hash)
	fatal("Object \"%s\" not found in object table but next link not null\n",
	      ob->name);
    ob->next_hash = obj_table[h];
    obj_table[h] = ob;
    objs_in_table++;
}

/*
 * Remove an object from the table - generally called when it
 * is removed from the next_all list - i.e. in destruct.
 */

void 
remove_object_hash(struct object *ob)
{
    struct object * s;
    int h = ObjHash(ob->name);
    
    s = find_obj_n(ob->name);
    
    if (s != ob)
	fatal("Remove object \"%s\": found a different object!\n",
	      ob->name);
    
    obj_table[h] = ob->next_hash;
    ob->next_hash = 0;
    objs_in_table--;

    if (ob->flags & O_CLONE)
	ob->prog->num_clones--;
    if (ob->prog->clones == ob) {
	if (ob->next_all->prog == ob->prog &&
	    ob->next_all->flags & O_CLONE &&
	    ob->next_all != ob)
	    ob->prog->clones = ob->next_all;
	else
	    ob->prog->clones = NULL;
    }
    if (ob->next_all == ob)
	obj_list = NULL;
    else if (ob == obj_list)
	obj_list = ob->next_all;

    ob->prev_all->next_all = ob->next_all;
    ob->next_all->prev_all = ob->prev_all;
    
}

/*
 * Lookup an object in the hash table; if it isn't there, return null.
 * This is only different to find_object_n in that it collects different
 * stats; more finds are actually done than the user ever asks for.
 */

static int user_obj_lookups = 0, user_obj_found = 0;

struct object * 
lookup_object_hash(char *s)
{
	struct object * ob = find_obj_n(s);
	user_obj_lookups++;
	if (ob) user_obj_found++;
	return(ob);
}

/*
 * Print stats, returns the total size of the object table.  All objects
 * are in table, so their size is included as well.
 */
void
add_otable_status(char *debinf)
{
    (void)strcat(debinf, "\nObject name hash table status:\n");
    (void)strcat(debinf, "------------------------------\n");
    (void)sprintf(debinf + strlen(debinf), "Average hash chain length            %.2f\n",
		  (double)objs_in_table / OTABLE_SIZE);
    (void)sprintf(debinf + strlen(debinf), "Searches/average search length       %lld (%.2f)\n",
		  obj_searches, (double)obj_probes / (double)obj_searches);
    (void)sprintf(debinf + strlen(debinf), "External lookups succeeded (succeed) %d (%d)\n",
		  user_obj_lookups, user_obj_found);
}

#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void
clear_otable()
{
    int i;

    for (i = 0; i < OTABLE_SIZE; i++)
	obj_table[i] = NULL;
}
#endif
