/* (c) Copyright by Anders Chrigstroem 1993, All rights reserved */
/* Permission is granted to use this source code and any executables
 * created from this source code as part of the CD Gamedriver as long
 * as it is not used in any way whatsoever for monetary gain. */

#include <memory.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <math.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "interpret.h"
#include "exec.h"
#include "object.h"
#include "mudstat.h"
#include "main.h"
#include "simulate.h"
#include "backend.h"
#include "call_out.h"

void init_call_out(void);
void call_out(struct timeval *);
static void run_call_out(struct object *ob);

#define SLOT(when) (((long long)((when) * TIME_RES)) & ((NUM_SLOTS) - 1))
#define FUNC_NAME(ob, cop)\
(ob->prog->inherit[cop->inherit].prog->functions[cop->fun].name)

struct call {
    struct call *next;
    struct closure *func;
    double reload;
    double when;
    long long id;
#ifdef THIS_PLAYER_IN_CALLOUT
    struct object *command_giver;
#endif
};

static struct object *call_outs[NUM_SLOTS];
static double last;

long long num_call;
long long call_out_size;
static long long call_id;

void
init_call_out()
{
    call_id = 1;
    last = 0;
#if TIME_RES * 4 >= NUM_SLOTS 
# error There must be more slots in the call_out table
#endif
}
static void
remove_ob(struct object *ob) {
    struct object **obp;

    if (ob->call_outs && !ob->callout_task)
    {
	int slot = SLOT(ob->call_outs->when);
	for (obp = &(call_outs[slot]); *obp && *obp != ob;
	     obp = &(*obp)->next_call_out) ;
	if (!*obp)
	    return; /* It's not on the list */
	*obp = ob->next_call_out;
	ob->next_call_out = 0;
    }
}
static void
add_ob(struct object *ob) {
    struct object **obp;

    if (ob->next_call_out)
	abort();
    if (ob->call_outs && !ob->callout_task)
    {
        int slot = SLOT(ob->call_outs->when);
        for (obp = &(call_outs[slot]); *obp &&
             (*obp)->call_outs->when < ob->call_outs->when;
             obp = &(*obp)->next_call_out) ;
        ob->next_call_out = *obp;
        *obp = ob;
    }
}    

void
call_out_swap_objects(struct object *ob1, struct object *ob2)
{
    struct call *co;
    struct task *task;
    short num_callouts;
    
    remove_ob(ob1);
    remove_ob(ob2);
    
    co = ob1->call_outs;
    task = ob1->callout_task;
    num_callouts = ob1->num_callouts;
    ob1->call_outs = ob2->call_outs;
    ob1->callout_task = ob2->callout_task;
    ob1->num_callouts = ob2->num_callouts;
    if (ob1->callout_task)
	ob1->callout_task->arg = ob1;

    ob2->call_outs = co;
    ob2->callout_task = task;
    ob2->num_callouts = num_callouts;
    if (ob2->callout_task)
	ob2->callout_task->arg = ob2;

    add_ob(ob1);
    add_ob(ob2);
}

static void
insert_call_out(struct object *ob, struct call *cop)
{
    struct call **copp;
     
    if (!ob->call_outs || cop->when < ob->call_outs->when) {
	remove_ob(ob);
	cop->next = ob->call_outs;
	ob->call_outs = cop;
	if (!ob->callout_task) {
	    if (cop->when <= last)
		ob->callout_task = create_task((void (*)(void *))run_call_out, ob);
	    else
		add_ob(ob);
	}
	return;
    }
    /* Insert the callout */
    for (copp = &ob->call_outs; *copp && cop->when >= (*copp)->when;
	 copp = &(*copp)->next) ;
    cop->next = *copp;
    *copp = cop;

}

static void
free_call(struct call *cop)
{
    free_closure(cop->func);
#ifdef THIS_PLAYER_IN_CALLOUT
    if (cop->command_giver)
	free_object(cop->command_giver, "free_call");
#endif
    free((char *)cop);
    num_call--;
    call_out_size -= sizeof(struct call);
}

long long
new_call_out(struct closure *fun, double delay, double reload)
{
    struct call *cop;

    if (delay < 0)
	delay = 0;

    if (reload < 0)
	reload = -1;

    if (current_object->num_callouts >= MAX_CALL_OUT) {
	delete_all_calls(current_object);
	error("too many alarms in object\n");
    }

    cop = (struct call *)xalloc(sizeof(struct call));
    call_out_size += sizeof(struct call);

    cop->func = fun;
    INCREF(fun->ref);
    cop->reload = reload;
    cop->when = alarm_time + delay;
    
    cop->id = call_id++;
#ifdef THIS_PLAYER_IN_CALLOUT
    cop->command_giver = command_giver;
    if (command_giver)
	INCREF(command_giver->ref);
#endif
    current_object->num_callouts++;
    num_call++;
    insert_call_out(current_object, cop);
    return cop->id;
}

void
delete_call(struct object *ob, long long call_ident)
{
    struct call **copp, *cop;
    int remove_first = 0;

    if (ob->call_outs && ob->call_outs->id == call_ident) {
	remove_first = 1;
	remove_ob(ob);
    }
    for(copp = &ob->call_outs; *copp && (*copp)->id != call_ident;
	copp = &(*copp)->next) ;
    if (!*copp)
	return;
    
    cop = *copp;
    *copp = (*copp)->next;
    free_call(cop);
    ob->num_callouts--;
    if (remove_first) {
	add_ob(ob);
    }
}

void
delete_all_calls(struct object *ob)
{
    struct call *next, *cop;
    
    remove_ob(ob);
    remove_task(ob->callout_task);
    ob->callout_task = NULL;
    for(cop = ob->call_outs; cop; cop = next)
    {
	next = cop->next;
	free_call(cop);
    }
    ob->call_outs = 0;
    ob->num_callouts = 0;
    ob->next_call_out = 0;
}

/*
  0 : call id;
  1 : function;
  2 : time left;
  3 : reload time;
  4 : argument;
*/

struct vector *
get_call(struct object *ob, long long call_ident)
{
    struct vector *val;
    struct call *cop;
    
    for(cop = ob->call_outs; cop && cop->id != call_ident; cop = cop->next)
	;
    if (!cop)
	return 0;
    
    val = allocate_array(5);
    
    val->item[0].type = T_NUMBER;
    val->item[0].u.number = cop->id;
    
    val->item[1].type = T_STRING;
    val->item[1].string_type = STRING_MSTRING;
    val->item[1].u.string = make_mstring(getclosurename(cop->func));
    
    val->item[2].type = T_FLOAT;
    val->item[2].u.real = cop->when - alarm_time;
    if (val->item[2].u.real < 0.0)
	val->item[2].u.real = 0.0;
    val->item[3].type = T_FLOAT;
    val->item[3].u.real = cop->reload;
    
    val->item[4].type = T_POINTER;
    val->item[4].u.vec = cop->func->funargs;
    INCREF(cop->func->funargs->ref);
    
    return val;
}
    
/*
  0 : call id;
  1 : function;
  2 : time left;
  3 : reload time;
  4 : argument;
*/
struct vector *
get_calls(struct object *ob)
{
    struct vector *ret;
    struct call *cop;
    int i;

    for(i = 0, cop = ob->call_outs; cop; i++, cop = cop->next)
	;
    ret = allocate_array(i);
    
    for(i = 0, cop = ob->call_outs; cop; i++, cop = cop->next)
    {
	struct vector *val;
	
	val = allocate_array(5);

	val->item[0].type = T_NUMBER;
	val->item[0].u.number = cop->id;
	
	val->item[1].type = T_STRING;
	val->item[1].string_type = STRING_MSTRING;
	val->item[1].u.string = make_mstring(getclosurename(cop->func));
	
	val->item[2].type = T_FLOAT ;
	val->item[2].u.real = cop->when - alarm_time;
	if (val->item[2].u.real < 0.0)
	    val->item[2].u.real = 0.0;
	
	val->item[3].type = T_FLOAT;
	val->item[3].u.real = cop->reload;
	
	val->item[4].type = T_POINTER;
	val->item[4].u.vec = cop->func->funargs;
	INCREF(cop->func->funargs->ref);
	
	ret->item[i].type = T_POINTER;
	ret->item[i].u.vec = val;
    }
    return ret;
}

long long current_call_out_id;
struct object *current_call_out_object;

int inhibitcallouts = 0;	/* Stop call-out processing. */

/*
 * last_alarm is at which slot we last executed an alarm
 */
static void
next_call_out(struct timeval *tvp)
{
    unsigned int end, slot;

    /*
     * We only want to scan 3 seconds into the future
     */
    end = SLOT(last + 3.0) + 1;
    if (end == NUM_SLOTS)
        end = 0;
    slot = SLOT(last);
    do {
	/*
	 * We found an alarm...
	 */
	if (call_outs[slot]) {
	    /*
	     * We found an alarm that was scheduled to run already
	     */
	    if (call_outs[slot]->call_outs->when < last) {
		tvp->tv_sec = 0;
		tvp->tv_usec = 0;
		return;
	    }
	    /*
	     * We found an alarm that is scheduled to run within the next
	     * 3 seconds
	     */
	    else if (call_outs[slot]->call_outs->when - last < 3.0) {
		double delay = call_outs[slot]->call_outs->when - last; 
		tvp->tv_sec = delay;
		tvp->tv_usec = (delay - tvp->tv_sec) * 1e6 + 0.5;
		return;
	    }
	}
	slot = (slot + 1) & (NUM_SLOTS - 1);
    } while (slot != end);
    /* Failed to find a call_out. Do extensive search */
    tvp->tv_sec = 3; /* default to 3 seconds */
    tvp->tv_usec =0;
}

static void
run_call_out(struct object *ob)
{
    static struct call *cop, **copp;
    struct gdexception exception_frame;
    char caodesc[1024];
    struct closure *thefun;
    
    if (inhibitcallouts || !ob->call_outs ||
	ob->call_outs->when > alarm_time) {
	ob->callout_task = 0;
        add_ob(ob);
	return;
    }
    /* Get one callout */
    cop = ob->call_outs;
    ob->call_outs = cop->next;
    if (cop->reload >= 0) /* Reschedule the callout */
    {
	current_call_out_id = cop->id;
	if (cop->reload < 1e-12)
	    cop->when = alarm_time;
	else
	    cop->when = alarm_time - fmod(alarm_time - cop->when, cop->reload) +
		cop->reload;
	if (cop->when < alarm_time)
	    cop->when = alarm_time;

	for (copp = &ob->call_outs;
	     *copp && (*copp)->when < cop->when;
	     copp = &(*copp)->next) ;
	cop->next = *copp;
	*copp = cop;
    }
    else
	current_call_out_id = 0;

    /* do the call */
#ifdef THIS_PLAYER_IN_CALLOUT
    if (cop->command_giver &&
	cop->command_giver->flags & O_DESTRUCTED)
    {
	free_object(cop->command_giver, "call_out");
	cop->command_giver = 0;
    }
	    
	    
    if (cop->command_giver &&
	cop->command_giver->flags & O_ENABLE_COMMANDS)
	command_giver = cop->command_giver;
    else if (ob->flags & O_ENABLE_COMMANDS)
	command_giver = ob;
    else
#endif
	command_giver = 0;
    if (s_flag)
	reset_mudstatus();
    eval_cost = 0;

    current_call_out_object = current_object = ob;
    INCREF(current_call_out_object->ref);
    current_interactive = 0;
    exception_frame.e_exception = exception;
    exception_frame.e_catch = 0;
    exception = &exception_frame;
	    
    thefun = cop->func;
    INCREF(thefun->ref); /* keep a reference */
    if (cop->reload < 0) {
	free_call(cop);
	ob->num_callouts--;
    }
    if (thefun->funobj != ob)
	previous_ob = ob;
	    
    if (setjmp(exception_frame.e_context)) {
	exception = exception_frame.e_exception;
	clear_state();
	debug_message("Error in call out.\n");
	if (current_call_out_id)
	{
	    /*access_program(ob->prog->inherit[inh].prog);*/
	    debug_message("Call out %s turned off in %s.\n", getclosurename(thefun),
			  current_call_out_object->name);
	    delete_call(current_call_out_object, current_call_out_id);
	}
    }
    else
    {
	update_alarm_av();
	(void)call_var(0, thefun);
	exception = exception_frame.e_exception;
	pop_stack();
		
	if (s_flag)
	{
	    strcpy(caodesc, "CAO:");
	    strncat(caodesc, show_closure(thefun),
		    sizeof(caodesc) - 5);
	    print_mudstatus(caodesc, eval_cost, 
			    get_millitime(), get_processtime());
	}
    }
    free_closure(thefun); /* and kill the extra reference */
    free_object(current_call_out_object, "call_out");
    previous_ob = 0;
    /* Reschedule the object */
    if (ob->call_outs && (ob->call_outs->when <= alarm_time ||
			  ob->call_outs->when <= last)) {
	reschedule_task(ob->callout_task);
	return;
    }
    ob->callout_task = 0;
    add_ob(ob);
}

void
call_out(struct timeval *tvp)
{
    struct object *ob;
    double last_end;
    int end = 0;
    
    if (inhibitcallouts) {
	if (tvp) {
	    tvp->tv_sec = 3;
	    tvp->tv_usec = 0;
	}
	return;
    }

    while (!end)
    {
	int slot, slot_end;
	
	if (last > alarm_time)
	    break;
	last_end = last + 2.0;
	if (last_end > alarm_time) {
	    last_end = alarm_time; /* last lap */
	    end = 1;
	}
	slot = SLOT(last);
	slot_end = (SLOT(last_end) + 1) & (NUM_SLOTS - 1);
	for (; slot != slot_end; slot = ((slot + 1) & (NUM_SLOTS - 1)))
	    while(call_outs[slot] &&
		  call_outs[slot]->call_outs->when <= last_end)
	    {
		/* Extract the object and the callout */
		ob = call_outs[slot];
		call_outs[slot] = ob->next_call_out;
		ob->next_call_out = 0;
		ob->callout_task = create_task((void (*)(void *))run_call_out, ob);
	    }
	last = last_end;
    }
    if (tvp)
       next_call_out(tvp);
}

int
dump_callouts(FILE *f)
{
    int count;
    struct object *ob;
    struct call *c;
#ifdef THIS_PLAYER_IN_CALLOUT
    char *giver;
#else
#define giver "(void)"
#endif
    
    count = 0;
    ob = obj_list;
    do {
	for (c = ob->call_outs; c; c = c->next)
	{
#ifdef THIS_PLAYER_IN_CALLOUT
	    if (!c->command_giver ||
		c->command_giver->flags & O_DESTRUCTED)
	    {
		giver = "(void)";
	    } else {
		giver = c->command_giver->name;
	    }
#endif
	    fprintf(f,
                    "%s %s %9.6f %9.6f %s\n",
                    ob->name,
                    show_closure(c->func),
                    c->when - alarm_time,
                    c->reload,
                    giver);
	    
	    count++;
	}
    } while ((ob = ob->next_all) != obj_list);
    
    return count;
}

#ifdef DEBUG
void
count_ref_from_call_outs()
{
    struct object *ob;
    struct call *cop;
    
#ifdef THIS_PLAYER_IN_CALLOUT
    ob = obj_list;
    do {
	for(cop = ob->call_outs; cop; cop = cop->next)
	{
	    if (cop->command_giver)
		cop->command_giver->extra_ref++;
	}
    } while ((ob = ob->next_all) != obj_list);
    
#endif
}
#endif

