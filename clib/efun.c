#include <stdio.h>
#include <string.h>

#include "../lint.h"
#include "../interface.h"
#include "../object.h"
#include "../mstring.h"
#include "../simulate.h"

/* Define variables */
extern struct object *previous_ob;
extern struct object *current_interactive;

/* Define functions */

/*
 * Description: The wellbeknownst quicksort algorithm,
 *              with recursion done by manual stack,
 *              and no special checkstuff.
 * Arguments:   fp[0] : The array to be sorted.
 *              fp[1] : An eventual function to compare items.
 * Returns:     The sorted array, on the interpreter stack.
 * Note:        This function messes with the original array,
 *              which is the same behaviour as the original
 *              simul_efun, and some code depends on it.
 */
#define QS_MAX_REC 32
#define A fp->u.vec->item

static void
efun_sort(struct svalue *fp)
{
    int l, c, r, i;
    int ss[QS_MAX_REC], es[QS_MAX_REC], p;
    int start, end;
    struct closure *compare = NULL;
    struct svalue h, t;

    if (fp[0].type == T_POINTER)
    {
	start = p = 0;
	end = fp[0].u.vec->size;
	if (fp[1].type == T_FUNCTION) {
	    compare = fp[1].u.func;
            if (compare->funtype == FUN_EFUN)
            {
                error("sort_array with efun function");
            }
            
	    while (p || (end - start) > 1)
	    {
		if ((end - start) < 2)
		{
		    start = ss[--p];
		    end = es[p];
		}
		else
		{
		    h = A[((start + end) / 2)];
		    for (c = l = start, r = end; c < r;)
		    {
			push_svalue(&A[c]);
			push_svalue(&h);
			(void)call_var(2, compare);
			i = ((sp->type == T_NUMBER) ? (sp->u.number) : 0);
			pop_stack();
			if (i < 0)
			{
			    t =  A[l];
			    A[l++] = A[c];
			    A[c++] = t;
			}
			else if (i > 0)
			{
			    t = A[--r];
			    A[r] = A[c];
			    A[c] = t;
			}
			else
			    c++;
		    }
		    if ((l - start) < (end - c))
		    {  /* Push the larger part, keep recursion to a minimum */
			ss[p] = c;
			es[p++] = end;
			end = l;
		    }
		    else
		    {
			ss[p] = start;
			es[p++] = l;
			start = c;
		    }
		}
	    }
	}
	else
	{ /* No compare function */
	    while (p || (end - start) > 1)
	    {
		if ((end - start) < 2)
		{
		    start = ss[--p];
		    end = es[p];
		}
		else
		{
		    h = A[((start + end) / 2)];
		    for (c = l = start, r = end; c < r;)
		    {
			switch ((A[c].type == h.type) ? A[c].type : 0)
			{
			    case T_NUMBER:
				i = (A[c].u.number - h.u.number);
				break;
			    case T_FLOAT:
				i = (((A[c].u.real - h.u.real) < 0) ?
					-1 :
					((A[c].u.real - h.u.real) > 0) ? 1 : 0);
				break;
			    case T_STRING:
				i = (strcmp(A[c].u.string, h.u.string ));
				break;
			    default:
				i =  (A[c].type - h.type);
			}
			if (i < 0)
			{
			    t =  A[l];
			    A[l++] = A[c];
			    A[c++] = t;
			}
			else if (i > 0)
			{
			    t = A[--r];
			    A[r] = A[c];
			    A[c] = t;
			}
			else
			    c++;
		    }
		    if ((l - start) < (end - c))
		    {  /* Push the larger part, keep recursion to a minimum */
			ss[p] = c;
			es[p++] = end;
			end = l;
		    }
		    else
		    {
			ss[p] = start;
			es[p++] = l;
			start = c;
		    }
		}
	    }
	}
    }
    push_svalue(fp);
}

static func func_sort =
{
    "sort",
    efun_sort
};

static void
efun_cat_file(struct svalue *fp)
{
    extern int read_file_len;
    char *str;
    
    if (fp[0].type != T_STRING || fp[1].type != T_NUMBER
	|| fp[2].type != T_NUMBER)
    {
	push_number(0);
	return;
    }
    str = read_file(fp[0].u.string, fp[1].u.number, fp[2].u.number);
    if (str)
    {
	push_mstring(str);
	if (command_giver)
	    (void)apply("catch_tell", command_giver, 1, 0);
	else
	    (void)apply("catch_tell", current_interactive, 1, 0);
	push_number(read_file_len);
    }
    else
	push_number(0);
}

static func func_cat_file = 
{
    "cat_file",
    efun_cat_file,
};

static void
efun_tell_room(struct svalue *fp)
{
    struct object *room = 0, *ob, *tp;
    struct svalue *oblist = NULL;
    int i, print, size;
    
    push_number(0);

    if (fp[3].type == T_OBJECT)
        tp = fp[3].u.ob; 
    else if (command_giver && !(command_giver->flags & O_DESTRUCTED))
	tp = command_giver;
    else
	tp = 0;
    
    if (fp[0].type == T_STRING)
	room = find_object2(fp[0].u.string);
    else if (fp[0].type == T_OBJECT)
	room = fp[0].u.ob;
    if (!room)
       return;
    switch (fp[2].type)
    {
    case T_OBJECT:
	size = 1;
	oblist = &fp[2];
	break;
    case T_POINTER:
	size = fp[2].u.vec->size;
	oblist = fp[2].u.vec->item;
	break;
    default:
	size = 0;
	break;
    }

    /* tell folx in room */
    for(ob = room->contains; ob; ob = ob->next_inv)
    {
	print = 1;
	for (i = 0; i < size; i++)
	    if (oblist[i].u.ob == ob)
	    {
		print = 0;
		break;
	    }
	/* before we push, check if it's a living */
	if ((ob->flags & O_ENABLE_COMMANDS) && print)
	{
	   push_svalue(&fp[1]);
	   push_object(tp); 
	   (void)apply("catch_msg", ob, 2, 0); 
	}
    }
    
    /* tell room */
    print = 1; ob = room;
    for (i = 0; i < size; i++)
	if (oblist[i].u.ob == ob)
	{
	    print = 0;
	    break;
	}
    if ((ob->flags & O_ENABLE_COMMANDS) && print)
    {
	push_svalue(&fp[1]);
	push_object(tp); 
	(void)apply("catch_msg", ob, 2, 0); 
    }
}


static func func_tell_room = 
{
    "tell_room",
    efun_tell_room,
};

static void
efun_write(struct svalue *fp)
{
    push_number(0);

    if (!command_giver)
    {
	if (fp[0].type == T_STRING)
	    (void)printf("%s", fp[0].u.string);
	return;
    }
    push_svalue(fp);
    (void)apply("catch_tell", command_giver, 1, 0);
}
static func func_write = 
{
    "write",
    efun_write,
 };


static void
efun_say(struct svalue *fp)
{
    struct object *tp, *ob;
    struct svalue tp_svalue, *oblist = NULL;

    int size, i;
    int print;

    push_number(0);
    
    if (command_giver && !(command_giver->flags & O_DESTRUCTED))
	tp = command_giver;
    else
	tp = previous_ob;

    tp_svalue.type=T_OBJECT; 
#ifdef PURIFY
    tp_svalue.string_type = -1;
#endif
    tp_svalue.u.ob=tp;    
    
    switch (fp[1].type)
    {
    case T_OBJECT:
	size = 1;
	oblist = &fp[1];
	break;
    case T_POINTER:
	size = fp[1].u.vec->size;
	oblist = fp[1].u.vec->item;
	break;
    default:
	if (tp)
	{
	   size = 1;
	   oblist = &tp_svalue;
	}
	else size = 0;
	break;
    }

    /* tell folx in room */
    if (tp && tp->super)
    {
	for (ob = tp->super->contains; ob; ob = ob->next_inv)
	{
	    print = 1;
	    for (i = 0; i < size; i++)
		if (oblist[i].u.ob == ob)
		    print = 0;
	    /* before we push, check if it's a living */
	    if ((ob->flags & O_ENABLE_COMMANDS) && print)
	    {
		push_svalue(fp);
		push_svalue(&tp_svalue); 
		(void)apply("catch_msg", ob, 2, 0); 
	    }
	}

	/* tell room */
	print = 1; ob = tp->super;
	for (i = 0; i < size; i++)
	    if (oblist[i].u.ob == ob)
		print = 0;
	if ((ob->flags & O_ENABLE_COMMANDS) && print)
	{
	    push_svalue(fp);
	    push_svalue(&tp_svalue); 
	    (void)apply("catch_msg", ob, 2, 0); 
	}
    }
    
    /* tell folx in player */
    for (ob = tp->contains; ob; ob = ob->next_inv)
    {
	print = 1;
	for (i = 0; i < size; i++)
	    if (oblist[i].u.ob == ob)
		print = 0;
	/* before we push, check if it's a living */
	if ((ob->flags & O_ENABLE_COMMANDS) && print)
	{
	   push_svalue(fp);
	   push_svalue(&tp_svalue);
	   (void)apply("catch_msg", ob, 2, 0);
	}
    }

    /* tell this_player */
    print = 1; ob = tp;
    for (i = 0; i < size; i++)
       if (oblist[i].u.ob == ob)
	  print = 0;
    if (ob && (ob->flags & O_ENABLE_COMMANDS) && print)
    {
       push_svalue(fp);
       push_svalue(&tp_svalue); 
       (void)apply("catch_msg", ob, 2, 0); 
    }   
}

static func func_say = 
{
    "say",
    efun_say,
};

static void
efun_atoi(struct svalue *fp)
{
    if (fp->type == T_STRING)
	push_number(atoi(fp->u.string));
    else
	push_number(0);
}
static func func_atoi =
{
    "atoi",
    efun_atoi,
};

/* Define the interface */

static var *(vars[]) =
{
   0,
};

static func *(funcs[]) =
{
    &func_sort,
    &func_cat_file,
    &func_say,
    &func_write,
    &func_tell_room,
    &func_atoi,
    0,
};


struct interface efun = 
{
    "secure/simul_efun.c",
    vars,
    funcs,
};
