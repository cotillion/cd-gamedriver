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
    0,
};


struct interface stdobject = 
{
    "std/object.c",
    vars,
    funcs,
};

