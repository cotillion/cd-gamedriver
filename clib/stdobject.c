#include <stdio.h>
#include <string.h>
#if defined(sun) || defined(__osf__)
#include <alloca.h>
#endif

#include "../lint.h"
#include "../interface.h"
#include "../object.h"
#include "../mstring.h"
#include "../mapping.h"
#include "../simulate.h"

#include "../inline_svalue.h"

/* Define variables */
extern struct object *previous_ob;


/* Define functions */

static var obj_prev = { "obj_previous", 0 };
static var obj_props = { "obj_props", 0 };
static var obj_no_change = { "obj_no_change", 0 };

/*
fp[0]: string to be expanded
fp[1]: target object
*/
static void
object_check_call(struct svalue *fp)
{
    extern struct svalue const0;
    extern struct svalue *process_value (char *, int);
    extern char *process_string(char *, int);
    extern struct object *current_object;
    struct closure *fun;

    struct svalue *ret = NULL;
    char *p, *p2, *str, *retstr = NULL;

    if (fp[0].type != T_STRING && fp[0].type != T_FUNCTION)
    {
	push_svalue(&fp[0]);
	return;
    }

    assign_svalue(&VAR(obj_prev), &fp[1]);

    if (fp[0].type == T_FUNCTION)
    {
	fun = fp[0].u.func;
	(void)call_var(0, fun);
	assign_svalue(&VAR(obj_prev), &const0);
	return;
    }

    str = fp[0].u.string;
    if (str[0] == '@' && str[1] == '@')
    {
	p = &str[2];
	p2 = &p[-1];
	while ((p2 = (char *) strchr(&p2[1], '@')) &&
	       (p2[1] != '@'))
	    ;
	if (!p2)
	{
	    ret = process_value(p, 1);
	}
	else if (p2[2] == '\0')
	{
	    size_t len = strlen(p);
	    p2 = (char *) alloca(len - 1);
	    (void)strncpy(p2, p, len - 2);
	    p2[len - 2] = '\0';
	    ret = process_value(p2, 1);
	}
	else
	    retstr = process_string(str, 1);
    }
    else
        retstr = process_string(str, 1);

    if (ret)
    {
	push_svalue(ret);
    }
    else
    {
        if (retstr)
	    push_mstring(retstr);
        else
            push_svalue(&fp[0]);
    }

    assign_svalue(&VAR(obj_prev), &const0);
}

static func func_check_call =
{
    "check_call",
    object_check_call,
};
static void
object_usun_stare_nazwy(struct svalue *fp)
{
    struct vector *arr, *workarr, *argarr, *ret_nazwy, *ret_rodzaje;
    struct svalue el, *workitem, *rodzajitem;
    int size_arg, size_work, ix, cx, dx, arr_ix;

    if (fp[0].type != T_POINTER)
    {
        if (fp[0].type != T_STRING)
        {
            push_number(1);
            return;
        }
        
        arr = allocate_array(1);
        assign_svalue_no_free(&arr->item[0], fp);
        el.type = T_POINTER;
        el.u.vec = arr;
        assign_svalue(fp, &el);
    }
    else if (!fp[0].u.vec->size)
    {
        push_number(1);
        return;
    }
    
    argarr = fp[0].u.vec;
    
    if (!fp[2].u.number) // Liczba pojedyncza.
    {
        workitem = &(VAR(obj_names).u.vec->item[fp[1].u.number]);
        rodzajitem = &(VAR(obj_rodzaje).u.vec->item[fp[1].u.number]);
    }
    else
    {
        workitem = &(VAR(obj_pnames).u.vec->item[fp[1].u.number]);
        rodzajitem = &(VAR(obj_prodzaje).u.vec->item[fp[1].u.number]);
    }

    if (workitem->type != T_POINTER)
    {
	push_number(1);
	return;
    }
    
    workarr = workitem->u.vec;
    size_work = workarr->size;
    size_arg = argarr->size;
	
    if (!size_work)
    {
	push_number(1);
	return;
    }

#if 0    
    if (argarr->size == 1)
    {
	if ((argarr->item[0].type != T_STRING) ||
	    (workarr->item[0].type != T_STRING))
	   fprintf(stderr, "ROZNI SIE\n");
	if (!strcmp(argarr->item[0].u.string, workarr->item[0].u.string))
	{
	    push_number(0);
	    return;
	}
    }
#endif
    
    size_work--;
    
    ix = size_arg;
    while(--ix >= 0)
    {
        cx = size_work;
        while (cx >= 0)
        {
            if ((argarr->item[ix].type != T_STRING) ||
 		(workarr->item[cx].type != T_STRING))
 	    {
 		cx--;
 		continue;
 	    }
	    if (strcmp(argarr->item[ix].u.string, workarr->item[cx].u.string))
	    {
		cx--;
		continue;
	    }

	    ret_nazwy = allocate_array(size_work);
	    ret_rodzaje = allocate_array(size_work);
#if 0	    
            fprintf(stderr, "CKK 3, nazwa = '%s', '%s'\n", 
                argarr->item[ix].u.string, current_object->name);
#endif	    
	    arr_ix = 0;
	    for (dx = 0; dx < cx; dx++)
	    {
		assign_svalue_no_free(&ret_nazwy->item[arr_ix],
				      &workarr->item[dx]);
		assign_svalue_no_free(&ret_rodzaje->item[arr_ix++],
				      &(rodzajitem->u.vec->item[dx]));
	    }
	    for (dx = cx + 1; dx <= size_work; dx++)
	    {
		assign_svalue_no_free(&ret_nazwy->item[arr_ix],
				      &workarr->item[dx]);
		assign_svalue_no_free(&ret_rodzaje->item[arr_ix++],
				      &(rodzajitem->u.vec->item[dx]));
	    }

	    free_vector(workitem->u.vec);
	    free_vector(rodzajitem->u.vec);
	    
	    workitem->u.vec/* = workarr*/ = ret_nazwy;
	    rodzajitem->u.vec = ret_rodzaje;
	    break; // Zakladamy, ze nigdy nie bedzie dwoch takich samych nazw.
/*
	    size_work--;
	    cx--;
 */
        }
    }
    
    push_number(1);
    return;
}

static func func_usun_stare_nazwy =
{
    "usun_stare_nazwy",
    object_usun_stare_nazwy,
};

static void
object_query_prop(struct svalue *fp)
{
    extern struct svalue const0;
    extern struct svalue *process_value (char *, int);
    extern char *process_string(char *, int);
    extern struct object *current_object;

    struct svalue *ret = NULL;
    struct svalue *val;
    static struct svalue tmp;
    struct closure *fun;

    char *p, *p2, *str, *retstr = NULL;

    extern struct svalue *get_map_lvalue(struct mapping *,
					 struct svalue *, int );
    if (VAR(obj_props).type != T_MAPPING)
    {
	push_number(0);
	return;
    }

    val = get_map_lvalue(VAR(obj_props).u.map, fp, 0);
    if (val->type != T_STRING && val->type != T_FUNCTION)
    {
	if (val->type == T_OBJECT && (val->u.ob->flags & O_DESTRUCTED))
	    free_svalue(val);
	push_svalue(val);
	return;
    }

    if (previous_ob != NULL) {
	tmp.type = T_OBJECT;
	tmp.u.ob = previous_ob;
	assign_svalue(&VAR(obj_prev), &tmp);
    }
    else
	assign_svalue(&VAR(obj_prev), &const0);


    if (val->type == T_FUNCTION)
    {
	fun = val->u.func;

        if (!legal_closure(fun))
        {
            push_number(0);
            return;
        }

	(void)call_var(0, fun);
	assign_svalue(&VAR(obj_prev), &const0);
	return;
    }

    str = val->u.string;
    if (str[0] == '@' && str[1] == '@')
    {
	p = &str[2];
	p2 = &p[-1];
	while ((p2 = (char *) strchr(&p2[1], '@')) &&
	       (p2[1] != '@'))
	    ;
	if (!p2)
	{
	    ret = process_value(p, 1);
	}
	else if (p2[2] == '\0')
	{
	    size_t len = strlen(p);
	    p2 = (char *) tmpalloc(len - 1);
	    (void)strncpy(p2, p, len - 2);
	    p2[len - 2] = '\0';
	    ret = process_value(p2, 1);
	}
	else
	    retstr = process_string(str, 1);
    }
    else
        retstr = process_string(str, 1);

    if (ret)
    {
	push_svalue(ret);
    }
    else
    {
        if (retstr)
	    push_mstring(retstr);
        else
            push_svalue(val);
    }

    assign_svalue(&VAR(obj_prev), &const0);
}


static func func_query_prop =
{
    "query_prop",
    object_query_prop,
};

static void
object_add_prop(struct svalue *fp)
{
   extern struct svalue *get_map_lvalue(struct mapping *,
					 struct svalue *, int );
   struct svalue *oval;

   if (VAR(obj_no_change).u.number || VAR(obj_props).type != T_MAPPING)
   {
      push_number(0);
      return;
   }

   assign_svalue((oval=get_map_lvalue(VAR(obj_props).u.map,fp,1)),&fp[1]);

   if (current_object->super)
   {
      push_svalue(&fp[0]);
      push_svalue(get_map_lvalue(VAR(obj_props).u.map,fp,0));
      push_svalue(oval);
      (void)apply("notify_change_prop",current_object->super,3,0);
   }

   push_number(0);
   return;
}

static func func_add_prop =
{
    "add_prop",
    object_add_prop,
};

static func func_change_prop =
{
    "change_prop",
    object_add_prop,
};

/* Define the interface */
static var *(vars[]) =
{
    &obj_prev,
    &obj_props,
    &obj_no_change,
    0,
};

static func *(funcs[]) =
{
    &func_check_call,
    &func_query_prop,
    &func_add_prop,
    &func_change_prop,
    &func_usun_stare_nazwy,
    0,
};


struct interface stdobject =
{
    "std/object.c",
    vars,
    funcs,
};

