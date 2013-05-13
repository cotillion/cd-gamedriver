
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>		/* sys/types.h and netinet/in.h are here to enable include of comm.h below */
#include <sys/stat.h>
/* #include <netinet/in.h> Included in comm.h below */
#include <memory.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include <unistd.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "lang.h"
#include "exec.h"
#include "interpret.h"
#include "object.h"
#include "instrs.h"
#include "comm.h"
#include "mapping.h"
#include "mudstat.h"
#include "lex.h"
#include "comm1.h"
#include "simulate.h"
#include "main.h"
#include "sent.h"
#include "json.h"

#include "inline_eqs.h"
#include "inline_svalue.h"

#include "efun_table.h"

#define FLOATASGOP(lhs, op, rhs) { lhs op (double)rhs; }


struct fkntab
{
    char *name;
    unsigned short inherit_index;
    unsigned short function_index;
};


extern char *crypt(const char *key, const char *salt);
extern struct object *master_ob;

extern struct vector *inherit_list (struct object *);
struct svalue *sapply (char *, struct object *, int, int);
#ifdef TRACE_CODE
static void do_trace (char *, char *, char *);
static int strpref (char *, char *);
#endif
static int apply_low (char *, struct object *, int, int);
extern int do_rename (char *, char *);     
static int inter_sscanf (int);
extern int wildmat(char *, char *);
static struct closure *alloc_objclosure(int ftype, int funinh, int funno, struct object *ob, char *refstr, int usecache);
static struct closure *alloc_objclosurestr(int ftype, char *funstr, struct object *ob, char *refstr, int usecache);
INLINE int search_for_function(char *name, struct program *prog);
int s_f_f(char *name, struct program *prog);

extern struct object *previous_ob;
extern char *last_verb, *trig_verb;
struct program *current_prog;
extern int  s_flag, unlimited;
extern struct object *current_interactive;
int variable_index_found;
int variable_inherit_found;    
int variable_type_mod_found;
unsigned long long cache_tries = 0, cache_hits = 0;

int num_closures, total_closure_size;

#ifdef CACHE_STATS
long long call_cache_saves = 0;
long long global_cache_saves = 0;
long long searches_needed = 0;
long long searches_done = 0;

long long global_first_saves = 0;
long long call_first_saves = 0;
#endif

#ifdef PROFILE_LPC
int trace_calls;
FILE *trace_calls_file;
double last_execution;
#endif

static int tracedepth;
#define TRACE_CALL 1
#define TRACE_CALL_OTHER 2
#define TRACE_RETURN 4
#define TRACE_ARGS 8
#define TRACE_EXEC 16
#define TRACE_TOS 32
#define TRACE_APPLY 64
#define TRACE_OBJNAME 128
#define TRACE_HEART_BEAT 512
#define TRACETST(b) (current_interactive->interactive->trace_level & (b))
#define TRACEP(b) \
(current_interactive && current_interactive->interactive && TRACETST(b) && \
 (current_interactive->interactive->trace_prefix == 0 || \
  (current_object && strpref(current_interactive->interactive->trace_prefix, \
			     current_object->name))) )
    
/*
 * Inheritance:
 * An object X can inherit from another object Y. This is done with
 * the statement inherit "file";
 * The inherit statement will clone a copy of that file, call reset
 * in it, and set a pointer to Y from X.
 * Y has to be removed from the linked list of all objects.
 * All variables declared by Y will be copied to X, so that X has access
 * to them.
 *
 * If Y isnt loaded when it is needed, X will be discarded, and Y will be
 * loaded separetly. X will then be reloaded again.
 */
extern int d_flag;
    
extern int current_line, eval_cost;
    
/*
 * These are the registers used at runtime.
 * The control stack saves registers to be restored when a function
 * will return. That means that control_stack[0] will have almost no
 * interesting values, as it will terminate execution.
 */
char *pc;			/* Program pointer. */
static struct svalue *fp;	/* Pointer to first argument. */
struct svalue *sp;		/* Points to value of last push. */
int inh_offset;			/* Needed for inheritance */

struct svalue start_of_stack[EVALUATOR_STACK_SIZE];
struct svalue catch_value;	/* Used to throw an error to a catch */

static struct control_stack control_stack[MAX_TRACE];
struct control_stack *csp;	/* Points to last element pushed */

#ifdef COUNT_CALLS /* Temporary */
static int num_call_self, num_call_down, num_call_other;
#endif

/* These are set by search_for_function if it is successful 
 * function_inherit_found == num_inherit if in top program 
 * function_prog_found	  == Implied by inherit_found 
 */
int    		function_inherit_found 	= -1; 
struct 	program *function_prog_found	= 0; 	
int 		function_index_found	= -1;
unsigned  short	function_type_mod_found = 0;

/*
 * Information about assignments of values:
 *
 * There are three types of l-values: Local variables, global variables
 * and vector elements.
 *
 * The local variables are allocated on the stack together with the arguments.
 * the register 'frame_pointer' points to the first argument.
 *
 * The global variables must keep their values between executions, and
 * have space allocated at the creation of the object.
 *
 * Elements in vectors are similar to global variables. There is a reference
 * count to the whole vector, that states when to deallocate the vector.
 * The elements consists of 'struct svalue's, and will thus have to be freed
 * immediately when over written.
 */

/*
 * Probe to see if the stack can be safely extended.
 */
static INLINE void
probe_stack(int n)
{
    if (sp + n >= &start_of_stack[EVALUATOR_STACK_SIZE])
	error("stack overflow\n");
}

/*
 * Extend the stack and check for overflow.
 */
static INLINE void
extend_stack(void)
{
    probe_stack(1);
    ++sp;
}

/*
 * Push an object pointer on the stack. Note that the reference count is
 * incremented.
 * A destructed object must never be pushed onto the stack.
 */
INLINE void 
push_object(struct object *ob)
{
    extend_stack();
    if (ob != NULL && !(ob->flags & O_DESTRUCTED))
    {
	sp->type = T_OBJECT;
	sp->u.ob = ob;
	add_ref(ob, "push_object");
    }
    else
    {
	*sp = const0;
    }
}

/*
 * Push a number on the value stack.
 */
INLINE void 
push_number(long long n)
{
    extend_stack();
    sp->type = T_NUMBER;
    sp->u.number = n;
}

INLINE void 
push_float(double f)
{
    extend_stack();
    sp->type = T_FLOAT;
    sp->u.real = f;
}

INLINE void
push_function(struct closure *func, bool_t reference)
{
    extend_stack();
    sp->type = T_FUNCTION;
    sp->u.func = func;
    if (reference)
	INCREF(func->ref);
}

/*
 * Push a string on the value stack.
 */
INLINE void 
push_string(char *p, int type)
{
    extend_stack();
    sp->type = T_STRING;
    sp->string_type = type;
    switch(type) {
	case STRING_MSTRING:
	    sp->u.string = make_mstring(p);
	    break;
	case STRING_SSTRING:
	    sp->u.string = make_sstring(p);
	    break;
	case STRING_CSTRING:
	    sp->u.string = p;
	    break;
    }
}

/*
 * Get address to a valid global variable.
 */
static INLINE struct svalue *
find_value(int inh, int rnum)
{
    int num;
    if ((inh == 255 && rnum == 255))
    {
	error("Referencing undefined variable (inh == %d, var_num == %d, variables are %s).\n",
	      inh, rnum, current_object->variables?"defined":"undefined");
    }
    if (inh == 255)
	inh = 0;
    else
	inh -= current_prog->num_inherited - 1;
#ifdef DEBUG
    if (inh > 0)
	fatal("Illegal variable access, %d(off %d). See trace above.\n",
	      inh, current_prog->num_inherited);
    if (rnum > current_object->prog->inherit[inh_offset + inh].prog->num_variables - 1)
	fatal("Illegal variable access, variable %d(off %d, in %d). See trace above.\n",
	      rnum, current_object->prog->inherit[inh_offset + inh].prog->num_variables,
	      inh_offset + inh);
#endif
    num =  current_object->prog->
	inherit[inh_offset + inh].variable_index_offset + rnum;
    
    return &current_object->variables[num];
}

/*
 * Free the data that an svalue is pointing to. Not the svalue
 * itself.
 */
#if defined(PROFILE)
void 
free_svalue(struct svalue *v)
{
    if (v->type & T_LVALUE)
	return;
    switch (v->type) {
	case T_NUMBER:
	case T_FLOAT:
	    break;
	case T_STRING:
	    switch (v->string_type) {
		case STRING_MSTRING:
		    free_mstring(v->u.string);
		    break;
		case STRING_SSTRING:
		    free_sstring(v->u.string);
		    break;
		case STRING_CSTRING:
		    break;
	    }
	    break;
	case T_OBJECT:
	    free_object(v->u.ob, "free_svalue");
	    break;
	case T_POINTER:
	    free_vector(v->u.vec);
	    break;
	case T_MAPPING:
	    free_mapping(v->u.map);
	    break;
	case T_FUNCTION:
	    free_closure(v->u.func);
	    break;
	default:
	    fatal("Invalid value of variable!\n");
	    break;
    }
    *v = const0; /* marion - clear this value all away */
}

int
equal_svalue(const struct svalue *sval1, const struct svalue *sval2)
{
    if (sval1->type == T_NUMBER && sval1->u.number == 0 &&
	sval2->type == T_OBJECT && sval2->u.ob->flags & O_DESTRUCTED)
	return 1;
    else if (sval2->type == T_NUMBER && sval2->u.number == 0 &&
	     sval1->type == T_OBJECT && sval1->u.ob->flags & O_DESTRUCTED)
	return 1;
    else if (sval2->type == T_NUMBER && sval2->u.number == 0 &&
	     sval1->type == T_FUNCTION && !legal_closure(sval1->u.func))
	return 1;
    else if (sval2->type == T_NUMBER && sval2->u.number == 0 &&
	     sval1->type == T_FUNCTION && !legal_closure(sval1->u.func))
	return 1;
    else if (sval1->type != sval2->type)
	return 0;
    else switch (sval1->type) {
	case T_NUMBER:
	    return sval1->u.number == sval2->u.number;
	case T_POINTER:
	    return sval1->u.vec == sval2->u.vec;
	case T_MAPPING:
	    return sval1->u.map == sval2->u.map;
	case T_STRING:
	    return sval1->u.string == sval2->u.string ||
		   strcmp(sval1->u.string, sval2->u.string) == 0;
	case T_OBJECT:
	    return ((sval1->u.ob->flags & O_DESTRUCTED) && (sval2->u.ob->flags & O_DESTRUCTED)) ||
		   sval1->u.ob == sval2->u.ob;
	case T_FLOAT:
	    return sval1->u.real == sval2->u.real;
	case T_FUNCTION:
	    return sval1->u.func == sval2->u.func;
    }
    return 0;
}
#endif /* !PROFILE */

/*
 * Prepend a slash in front of a string.
 */
char *
add_slash(char *cp)
{
    size_t len;
    char *xp;

    len = strlen(cp) + 1;
    xp = allocate_mstring(len);
    xp[0] = '/';
    (void)memcpy(&xp[1], cp, len);
    return xp;
}

/*
 * Assign to a svalue.
 * This is done either when element in vector, or when to an identifier
 * (as all identifiers are kept in a vector pointed to by the object).
 */

INLINE void 
assign_svalue_no_free(struct svalue *to, struct svalue *from)
{
    if (to == from)
	return;
    
#ifdef DEBUG
    if (from == 0)
	fatal("Null pointer to assign_svalue().\n");
#endif
    *to = *from;
    switch(from->type) {
    case T_STRING:
	switch(from->string_type) {
	case STRING_MSTRING:
	    (void)reference_mstring(from->u.string);
	    break;
	case STRING_SSTRING:
	    (void)reference_sstring(from->u.string);
	    break;
        case STRING_CSTRING:
	    break;
	default:
	    fatal("Bad string type %d\n", from->string_type);
	}
	break;
    case T_OBJECT:
	add_ref(to->u.ob, "ass to var");
	break;
    case T_POINTER:
	INCREF(to->u.vec->ref);
	break;
    case T_MAPPING:
	INCREF(to->u.map->ref);
	break;
    case T_FUNCTION:
	INCREF(to->u.func->ref);
	break;
    }
}

INLINE void 
assign_svalue(struct svalue *dest, struct svalue *v)
{
    if (dest == v)
	return;
    
    /* First deallocate the previous value. */
    free_svalue(dest);
    assign_svalue_no_free(dest, v);
}

#if 0
/* This function has been replaced with a macro to save speed. Macro is defined
 * in interpret.h
 */
void 
push_svalue(struct svalue *v)
{
    extend_stack();
    assign_svalue_no_free(sp, v);
}
#endif

/*
 * Pop the top-most value of the stack.
 * Don't do this if it is a value that will be used afterwards, as the
 * data may be sent to free(), and destroyed.
 */
INLINE 
void pop_stack() 
{
#ifdef DEBUG
    if (sp < start_of_stack)
	fatal("Stack underflow.\n");
#endif
    free_svalue(sp--);
}

/*
 * Compute the address of an array element.
 */
INLINE static void 
push_indexed_lvalue(int needlval)
{
    struct svalue *i, *vec, *item;
    long long ind = 0;		/* = 0 to make Wall quiet */
    long long org_ind = 0;
    
    i = sp;
    vec = sp - 1;
    if (vec->type != T_MAPPING)
    {
	if (i->type != T_NUMBER)
	    error("Illegal index.\n");
        org_ind = ind = i->u.number;
    }
    switch(vec->type) {
    case T_STRING: {
	static struct svalue one_character;
	/* marion says: this is a crude part of code */
	pop_stack();
	one_character.type = T_NUMBER;

        if (ind < 0)
            ind = strlen(vec->u.string) + ind;
        
	if (ind > strlen(vec->u.string) || ind < 0)
	    one_character.u.number = 0;
	else
	    one_character.u.number = vec->u.string[ind];
	free_svalue(sp);
	sp->type = T_LVALUE;
	sp->u.lvalue = &one_character;
	break;}
    case T_POINTER:
	pop_stack();

        if (ind < 0)
            ind = vec->u.vec->size + ind;
        
	if (ind >= vec->u.vec->size || ind < 0)
	    error("Index out of bounds. Vector size: %d, index: %lld\n",
                vec->u.vec->size, org_ind);
	item = &vec->u.vec->item[ind];
	if (vec->u.vec->ref == 1) {
	    static struct svalue quickfix = { T_NUMBER };
	    /* marion says: but this is crude too */
	    /* marion blushes. */
	    assign_svalue (&quickfix, item);
	    item = &quickfix;
	}
        
	free_svalue(sp);	  /* This will make 'vec' invalid to use */
	sp->type = T_LVALUE;
	sp->u.lvalue = item;
	break;
    case T_MAPPING:
	item = get_map_lvalue(vec->u.map, i, needlval);
	pop_stack();
	if (vec->u.map->ref == 1) {
	    static struct svalue quickfix = { T_NUMBER };
	    assign_svalue (&quickfix, item);
	    item = &quickfix;
	}
	free_svalue(sp);	   /* This will make 'vec' invalid to use */
	sp->type = T_LVALUE;
	sp->u.lvalue = item;
	break;
    default:
	error("Indexing on illegal type.\n");
	break;
    }
}

#ifdef OPCPROF
#define MAXOPC 512
static int opcount[MAXOPC];
#endif

/*
 * Deallocate 'n' values from the stack.
 */
INLINE
void pop_n_elems(int n)
{
#ifdef DEBUG
    if (n < 0)
	fatal("pop_n_elems: %d elements.\n", n);
#endif
    for (; n > 0; n--)
	pop_stack();
}

char *
get_typename(int type)
{
    
    switch(type)
    {
    case T_NUMBER:
	return "Integer";
    case T_STRING:
	return "String";
    case T_POINTER:
	return "Array";
    case T_OBJECT:
	return "Object";
    case T_MAPPING:
	return "Mapping";
    case T_FLOAT:
	return "Float";
    case T_FUNCTION:
	return "Function";
    }
    
    return "Unknown";
}

void 
bad_arg(int arg, int instr, struct svalue *sv)
{
    error("Bad argument %d to %s(), received type was %s \n", arg, get_f_name(instr), get_typename(sv->type));
}

void 
bad_arg_op(int instr, struct svalue *arg1, struct svalue *arg2)
{
    error("Bad arguments to %s(), received types were %s and %s\n",
     get_f_name(instr), get_typename(arg1->type), get_typename(arg2->type));
}

#if defined(PROFILE_LPC)
/* profile_exp_mtimebase should equal (1.0 - exp(-1.0/profile_timebase)) */
static long double profile_timebase = 60.0l, profile_exp_mtimebase = 0x8.766dfa92ba7052ep-9l; 

long double
get_profile_timebase() {
    return profile_timebase;
}
void
set_profile_timebase(long double timebase) {
    if (timebase > 0.0) {
	profile_timebase = timebase;
	profile_exp_mtimebase = -expm1l(-1.0l / timebase);
    }
}

#define _UPDATE_AV_C(delta_time) (expl(-(delta_time) / (profile_timebase)))
#define _UPDATE_AV(avg, C, amt) \
    ((avg) = (avg) * (C) + \
     (amt) * (profile_exp_mtimebase))
#define UPDATE_AV(avg, delta_time, amt) (_UPDATE_AV((avg), _UPDATE_AV_C((delta_time)), (amt)))

double
current_cpu(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec +  tv.tv_usec * 1e-6;
}

void
update_prog_profile(struct program *prog, double now, double delta, double tot_delta)
{
    UPDATE_AV(prog->cpu_avg, (now - prog->last_avg_update), delta);
    prog->last_avg_update = now;
    prog->cpu += delta;
}

void
update_func_profile(struct function *funp, double now, double delta, double tot_delta, int calls)
{
    long double C = _UPDATE_AV_C((now - funp->last_call_update));
    funp->last_call_update = now;

    _UPDATE_AV(funp->avg_time, C, delta);
    funp->time_spent += delta;

    _UPDATE_AV(funp->avg_tot_time, C, tot_delta);
    funp->tot_time_spent += tot_delta;

    _UPDATE_AV(funp->avg_calls, C, calls);
    funp->num_calls += calls;
}
#endif

void
save_control_context(struct control_stack *csp1)
{
#ifdef DEBUG
    if (current_prog && pc - current_prog->program >= current_prog->program_size)
	fatal("Invalid offset during context save\n");
#endif
    csp1->ob = current_object;
    csp1->prev_ob = previous_ob;
    csp1->fp = fp;
    csp1->funp = 0;
    csp1->prog = current_prog;
    csp1->extern_call = 0;
    if (current_prog)
	csp1->pc = pc - current_prog->program;
#ifdef DEBUG
    else
	csp1->pc = 0xdeadbeef;
#endif
    csp1->inh_offset = inh_offset;
}

void 
push_control_stack(struct object *ob, struct program *prog, struct function *funp)
{
    if (csp >= &control_stack[MAX_TRACE-1])
	error("Too deep recursion.\n");
    csp++;
    save_control_context(csp);
    csp->funp = funp;	/* Only used for tracebacks */
#if defined(PROFILE_LPC)
    {
	double now = current_cpu();

	if (trace_calls) {
	    if (csp == control_stack)
		fprintf(trace_calls_file, "%.3f %.6f\n",
                        (now - last_execution) * 1000.0, now);
	    fprintf(trace_calls_file, "%*s%s %s %s()\n",
		    (csp - control_stack) * 4, "",
		    ob->name, prog->name, funp ? funp->name : "???");
	}
	if (csp != control_stack) {
	    csp[-1].frame_cpu += now - csp[-1].startcpu;
	}
	csp->frame_start = csp->startcpu = now;
	csp->frame_cpu = 0.0;
    }
#endif
}

/*
 * Pop the control stack one element, and restore registers.
 * extern_call must not be modified here, as it is used imediately after pop.
 */
void
restore_control_context(struct control_stack *csp1)
{
    current_object = csp1->ob;

    current_prog = csp1->prog;
    if(current_prog)
    {
	pc = current_prog->program + csp1->pc;
    }
    previous_ob = csp1->prev_ob;
    fp = csp1->fp;
    inh_offset = csp1->inh_offset;
}

void 
pop_control_stack() 
{
#ifdef DEBUG
    if (csp == control_stack - 1)
	fatal("Popped out of the control stack\n");
#endif
#if defined(PROFILE_LPC)
    {
	double now = current_cpu();
	double delta = csp->frame_cpu + (now - csp->startcpu);
	double tot_delta = now - csp->frame_start;
	if (current_prog)
	    update_prog_profile(current_prog, now, delta, tot_delta);
	if (csp->funp)
	    update_func_profile(csp->funp, now, delta, tot_delta, 1);
	if (csp != control_stack) {
	    csp[-1].startcpu = now;
	} else
	    last_execution = now;
	if (trace_calls) {
	    fprintf(trace_calls_file, "%*s--- %.3f / %.3f\n",
		    (csp - control_stack) * 4, "",
		    delta * 1000.0,
		    tot_delta * 1000.0);
            if (csp == control_stack)
                putc('\n', trace_calls_file);
        }
    }
#endif
    restore_control_context(csp);
    csp--;
}

/*
 * Push a pointer to a vector on the stack. Note that the reference count
 * is incremented. Newly created vectors normally have a reference count
 * initialized to 1.
 */
INLINE void 
push_vector(struct vector *v, bool_t reference)
{
    extend_stack();
    if (reference)
	INCREF(v->ref);
    sp->type = T_POINTER;
    sp->u.vec = v;
}

INLINE void 
push_mapping(struct mapping *v, bool_t reference)
{
    extend_stack();
    if (reference)
	INCREF(v->ref);
    sp->type = T_MAPPING;
    sp->u.map = v;
}

/*
 * Push a string on the stack that is already allocated.
 */
INLINE void
push_mstring(char *p)
{
    extend_stack();
    sp->type = T_STRING;
    sp->string_type = STRING_MSTRING;
    sp->u.string = p;
}

extern char *string_print_formatted (int , char *, int, struct svalue *);

#ifdef TRACE_CODE
static void 
do_trace_call(struct function *funp)
{
    do_trace("Call direct ", funp->name, " ");
    if (TRACETST(TRACE_ARGS))
    {
	int i;
	char buff[1024];

	(void)sprintf(buff, " with %d arguments: ", funp->num_arg);
	write_socket(buff, command_giver);
	for(i = 0; i < funp->num_arg; i++)
	    write_socket(string_print_formatted(0, "%O ", 1, &fp[i]),
			 command_giver);
    }
    write_socket("\n", command_giver);
}
#endif

static unsigned int previous_ob_access_time;

/*
 * Argument is the function to execute.
 * The function is located in current_prog.
 * There is a number of arguments on the stack. Normalize them and initialize
 * local variables, so that the called function is pleased.
 */
char *
setup_new_frame(struct function *funp)
{
    int called_args;
    unsigned short int npc;
    char *off;
    /* Remove excessive arguments, or put them in argv if applicable */

    previous_ob_access_time = current_object->time_of_ref;

    if (funp->type_flags & TYPE_MOD_TRUE_VARARGS)
    {
	if (csp->num_local_variables >= funp->num_arg)
	{
	    struct vector *v;
	    int i, narg;
	    v = allocate_array(narg = csp->num_local_variables -
			       (funp->num_arg - 1));
	    for (i = narg - 1; i >= 0; i--)
		assign_svalue_no_free(&v->item[narg - 1 - i], &sp[-i]);
	    pop_n_elems(narg);
	    push_vector(v, FALSE);
	    csp->num_local_variables -= narg - 1;
	}
    }
    else
	while(csp->num_local_variables > funp->num_arg)
	{
	    pop_stack();
	    csp->num_local_variables--;
	}
    /* Correct number of arguments and local variables */
    called_args = csp->num_local_variables;
    while(csp->num_local_variables < funp->num_arg + (int)funp->num_local)
    {
	push_number(0);
	csp->num_local_variables++;
    }
#ifdef DEBUG
    if (called_args > funp->num_arg)
	fatal("Error in seting up call frame!\n");
#endif
    if (called_args == funp->num_arg)
	npc = funp->offset + funp->num_arg * 2;
    else
    {
	off = current_prog->program + funp->offset + called_args * 2;
	((char *)&npc)[0] = off[0];
	((char *)&npc)[1] = off[1];
    }
    tracedepth++;
    fp = sp - csp->num_local_variables + 1;
#ifdef TRACE_CODE
    if (TRACEP(TRACE_CALL)) {
	do_trace_call(funp);
    }
#endif
    return current_prog->program + npc;
}

/* marion
 * maintain a small and inefficient stack of error recovery context
 * data structures.
 * This routine is called in three different ways:
 * push=-1	Pop the stack.
 * push=1	push the stack.
 * push=0	No error occured, so the pushed value does not have to be
 *		restored. The pushed value can simply be popped into the void.
 *
 * The stack is implemented as a linked list of stack-objects, allocated
 * from the heap, and deallocated when popped.
 */
void 
push_pop_error_context (int push)
{
    static struct error_context_stack
    {
	struct gdexception *exception;
	struct control_stack *save_csp;
	struct object *save_command_giver;
	struct svalue *save_sp;
	struct error_context_stack *next;
	struct control_stack cstack;    
    } *ecsp = 0, *p;
    
    if (push == 1) {
	/*
	 * Save some global variables that must be restored separately
	 * after a longjmp. The stack will have to be manually popped all
	 * the way.
	 */
	p = (struct error_context_stack *)xalloc (sizeof *p);
	save_control_context(&(p->cstack));
	p->save_sp = sp;
	p->save_csp = csp;	
	p->save_command_giver = command_giver;
	p->exception = exception;
	p->next = ecsp;
	ecsp = p;
    } else {
	p = ecsp;
	if (p == 0)
	    fatal("Catch: error context stack underflow\n");

	if (push == 0) {
#ifdef DEBUG
#if 0
	    if (csp != p->save_csp-1)
		fatal("Catch: Lost track of csp\n");
#endif
#endif
	    ;
	} else {
	    /* push == -1 !
	     * They did a throw() or error. That means that the control
	     * stack must be restored manually here.
	     */
#ifdef PROFILE_LPC
	    double now = current_cpu();
	    struct program *prog = current_prog;
	    csp->frame_cpu += (now - csp->startcpu);
	    for (;csp != p->save_csp; (prog = csp->prog), csp--) {
		double frame_tot_cpu = now - csp->frame_start;
		if (prog)
		    update_prog_profile(prog, now, csp->frame_cpu, frame_tot_cpu);
		if (csp->funp)
		    update_func_profile(csp->funp, now, csp->frame_cpu, frame_tot_cpu, 1);
		if (trace_calls) {
		    fprintf(trace_calls_file, "%*s--- %.3f / %.3f\n",
			    (csp - control_stack) * 4, "",
			    csp->frame_cpu * 1000.0,
			    frame_tot_cpu * 1000.0);
		}
	    }
	    csp->startcpu = now;
#else
	    csp = p->save_csp;
#endif
	    pop_n_elems (sp - p->save_sp);
	    command_giver = p->save_command_giver;
	}
	exception = p->exception;
	ecsp = p->next;
	restore_control_context(&(p->cstack));
	free ((char *)p);
    }
}

/*
 * May current_object shadow object ob ? We rely heavily on the fact that
 * function names are pointers to shared strings, which means that equality
 * can be tested simply through pointer comparison.
 */
int 
validate_shadowing(struct object *ob)
{
    struct program *shadow = current_object->prog, *victim = ob->prog;
    struct svalue *ret;
    int inh;

    if (current_object->shadowing)
        error("shadow: Already shadowing.\n");
    if (current_object->shadowed)
	error("shadow: Can't shadow when shadowed.\n");
    if (current_object->super)
	error("The shadow must not reside inside another object.\n");
    if (ob->shadowing || current_object == ob)
	error("Can't shadow a shadow.\n");

    /* Loop structure copied from search_for_function... *shrug* /Dark */
    for (inh = ob->prog->num_inherited - 1; inh >= 0; inh--) 
    {
	struct program *progp = victim->inherit[inh].prog;
	int fun;

	if (progp->flags & PRAGMA_NO_SHADOW)
	    return 0;
	for (fun = progp->num_functions - 1; fun >= 0; fun--) 
	{
	    /* Should static functions 'shadowing' nomask functions
	     * be allowed? They do not do any harm... 
	     */
 	    if ((progp->functions[fun].type_flags & TYPE_MOD_NO_MASK) &&
		search_for_function(progp->functions[fun].name, shadow))
		error("Illegal to shadow 'nomask' function \"%s\".\n",
		      progp->functions[fun].name);
	}
    }

    if (current_object == master_ob)
	return 1;

    push_object(ob);
    ret = apply_master_ob(M_QUERY_ALLOW_SHADOW, 1);
    if (!(ob->flags & O_DESTRUCTED) &&
	ret && !(ret->type == T_NUMBER && ret->u.number == 0))
    {
	return 1;
    }
    return 0;
}

/*
 * When a vector is given as argument to an efun, all items has to be
 * checked if there would be an destructed object.
 * A bad problem currently is that a vector can contain another vector, so this
 * should be tested too. But, there is currently no prevention against
 * recursive vectors, which means that this can not be tested. Thus, the game
 * may crash if a vector contains a vector that contains a destructed object
 * and this top-most vector is used as an argument to an efun.
 */
/* The game wont crash when doing simple operations like assign_svalue
 * on a destructed object. You have to watch out, of course, that you dont
 * apply a function to it.
 * to save space it is preferable that destructed objects are freed soon.
 *   amylaar
 */
void 
check_for_destr(struct svalue *arg)
{
    int i, change;
    struct vector *v;
    struct mapping *m;
    struct apair *p, **pp;
    
    switch (arg->type)
    {
    case T_FUNCTION:
	v = arg->u.func->funargs;
	goto arrtest;
    case T_POINTER:
	v = arg->u.vec;
    arrtest:
	for (i = 0; i < v->size; i++) 
	{
	    if (v->item[i].type == T_OBJECT)
	    {
		if (!(v->item[i].u.ob->flags & O_DESTRUCTED))
		    continue;
	    }
	    else if (v->item[i].type == T_FUNCTION && legal_closure(v->item[i].u.func))
		continue;
	    else
		continue;
	    assign_svalue(&v->item[i], &const0);
	}
	break;
	
    case T_MAPPING:
	m = arg->u.map;
	/* Value parts that have been destructed are kept but set = 0. */
	for (i = 0 ; i < m->size ; i++) {
	    for (p = m->pairs[i]; p ; p = p->next) 
	    {
		if ((p->val.type == T_OBJECT && 
		     (p->val.u.ob->flags & O_DESTRUCTED)) ||
		    (p->val.type == T_FUNCTION &&
		     !legal_closure(p->val.u.func)))
		    assign_svalue(&p->val, &const0);
	    }
	}
	/* Index parts that has been destructed is removed */
	change = 1;
	do 
	{
	    for (i = 0 ; i < m->size ; i++) 
	    {
		for (pp = &m->pairs[i]; *pp; )
		{
		    p = *pp;
		    if ((p->arg.type == T_OBJECT &&
			 (p->arg.u.ob->flags & O_DESTRUCTED)) ||
			(p->arg.type == T_FUNCTION &&
			 !legal_closure(p->arg.u.func)))
		    {
			*pp = p->next;
			free_svalue(&p->arg);
			free_svalue(&p->val);
			free((char *)p);
			m->card--;
		    } 
		    else
		    {
			pp = &p->next;
		    }
		}
	    }
	    change = 0;
	} while (change != 0);
	break;
	
    default:
	error("Strange type to check_for_destr.\n");
	break;
    }
}

/*
 * Evaluate instructions at address 'p'. All program offsets are
 * to current_prog->program. 'current_prog' must be setup before
 * call of this function.
 *
 * There must not be destructed objects on the stack. The destruct_object()
 * function will automatically remove all occurences. The effect is that
 * all called efuns knows that they wont have destructed objects as
 * arguments.
 */
#ifdef TRACE_CODE
#define TRACE_SIZE 0x400
unsigned int previous_instruction[TRACE_SIZE];
struct program *previous_prog[TRACE_SIZE];
int stack_size[TRACE_SIZE];
unsigned int previous_pc[TRACE_SIZE];
int curtracedepth[TRACE_SIZE];
static unsigned int last;
#endif
/*static int num_arg; */
extern char *string_print_formatted (int , char *, int, struct svalue *);
extern char *break_string (char *, int, struct svalue *);
extern struct mapping *copy_mapping(struct mapping *);
extern char *query_ip_number (struct object *);
extern char *query_ip_name (struct object *);
extern struct vector *get_local_commands (struct object *);
extern struct vector *subtract_array (struct vector *,struct vector*);
extern struct vector *intersect_array (struct vector *, struct vector *);
extern struct vector *union_array (struct vector *, struct vector *);
extern char *string_print_formatted (int, char *, int, struct svalue *);
extern struct svalue *debug_command (char *, int, struct svalue *);
extern struct vector *subtract_array (struct vector*,struct vector*);
extern struct vector *intersect_array (struct vector*,struct vector*);
extern struct vector *make_unique (struct vector *arr, struct closure *fun, struct svalue *skipnum);
static void eval_instruction(char *);


static unsigned short read_short(char *addr)
{
    unsigned short ret;

    ((char *)&ret)[0] = ((char *)addr)[0];
    ((char *)&ret)[1] = ((char *)addr)[1];

    return ret;
}
/* ARGSUSED */
static void
f_last_reference_time(int xxx)
{
    push_number((long long)previous_ob_access_time);
}

/* ARGSUSED */
static void
f_ext(int xxx)
{
    fatal("f_ext should not be called.\n");
}

/* ARGSUSED */
static void
f_call_virt(int xxx)
{
    unsigned short fix, fiix;
    struct function *funp;
    int num_arg;
    char *func;

#ifdef COUNT_CALLS
    num_call_self++;
#endif
    cache_tries++;
    
    fiix = EXTRACT_UCHAR(pc);
    pc++;
    ((char *)&fix)[0] = pc[0];
    ((char *)&fix)[1] = pc[1];
    pc += 2;
    
    num_arg = EXTRACT_UCHAR(pc);
    pc++;

    if (current_object->prog == current_prog)
    {
	cache_hits++;
#ifdef CACHE_STATS
	call_first_saves += current_prog->num_inherited - fiix;
#endif	
	function_prog_found = current_object->prog->
	    inherit[fiix].prog;
	function_inherit_found = fiix;
	function_index_found = fix;
    }
    else
    {
	func = current_prog->inherit[fiix].prog->functions[fix].name;
	(void)s_f_f(func, current_object->prog);
	if (function_type_mod_found & TYPE_MOD_PRIVATE &&
	    inh_offset < function_inherit_found -
	    (int)function_prog_found->num_inherited + 1)
	    error("Attempted call of private function.\n");
    }

    funp = &(function_prog_found->functions[function_index_found]);
    /* Urgle. There should probably be a function for all this. |D| 
     * See call-self for comments 
     */
    push_control_stack (current_object, function_prog_found, funp);
    inh_offset = function_inherit_found;
    current_prog = function_prog_found;
    csp->ext_call = 0;
    csp->num_local_variables = num_arg;
    pc = setup_new_frame(funp);
    csp->extern_call = 0;
    
}

static void
f_call_self(int num_arg)
{
    struct function *funp;
    struct svalue *arg;

    arg = sp - num_arg + 1;

    if (search_for_function(arg->u.string, current_object->prog) == 0 ||
	((function_type_mod_found & TYPE_MOD_PRIVATE) &&
	inh_offset < function_inherit_found -
	(int)function_prog_found->num_inherited + 1))
    {
	/* No such function */
	pop_n_elems(num_arg);
	push_number(0);
	return;
    }

    free_svalue(arg);
    num_arg--;
    (void)memmove(arg, &arg[1], num_arg * sizeof(struct svalue));
    sp--;
    
    funp = &(function_prog_found->functions[function_index_found]);
    /* Urgle. There should probably be a function for all this. |D| 
     * See call-self for comments 
     */
    push_control_stack (current_object, function_prog_found, funp);
    inh_offset = function_inherit_found;
    current_prog = function_prog_found;
    csp->ext_call = 0;
    csp->num_local_variables = num_arg;
    pc = setup_new_frame(funp);
    csp->extern_call = 0;
    
}

/* ARGSUSED */
static void
f_call_selfv(int xxx)
{
    struct vector *argv = sp->u.vec;
    int i;
    int num_arg;

    INCREF(argv->ref);
    pop_stack();
    num_arg = argv->size + 1;
    for(i = 0; i < argv->size; i++)
    {
	push_svalue(&argv->item[i]);
    }
    free_vector(argv);
    f_call_self(num_arg);
}

/* ARGSUSED */
static void
f_call_non_virt(int xxx)
{
    /* Receives: char   index into inherit-list
     *           short  function name (index into program strings).
     *           char   number of arguments
     */
    struct function *funp;
    int inh, num_arg;
    unsigned short fix, fiix;
    
#ifdef COUNT_CALLS
    num_call_down++;
#endif
    
    fiix = EXTRACT_UCHAR(pc);
    pc++;
    ((char *)&fix)[0] = pc[0];
    ((char *)&fix)[1] = pc[1];
    pc += 2;
    
    num_arg = EXTRACT_UCHAR(pc);
    pc++;
    inh = fiix - (current_prog->num_inherited - 1);
    function_prog_found = current_prog->inherit[fiix].prog;
    funp = &(function_prog_found->functions[fix]);
    
    /* Urgle. There should probably be a function for all this. |D| 
     * See call-self for comments 
     */
    push_control_stack (current_object, function_prog_found, funp);
    inh_offset += inh;
    current_prog = function_prog_found;
    csp->ext_call = 0;
    csp->num_local_variables = num_arg;
    pc = setup_new_frame(funp);
    csp->extern_call = 0;    
}

/* ARGSUSED */
static void
f_call_c(int num_arg)
{
    void (*func)(struct svalue *);
    memcpy(&func, pc, sizeof(func));
    pc += sizeof(func);
    func(fp);
}

/* ARGSUSED */
static void
f_call_simul(int xxx)
{
    unsigned short func_name_index;
    int num_arg;
    char *func;
    pc++;

    ((char *)&func_name_index)[0] = pc[0];
    ((char *)&func_name_index)[1] = pc[1];
    pc += 2;
    num_arg = EXTRACT_UCHAR(pc);
    pc++;

    func = current_prog->rodata + func_name_index;
    if (!simul_efun_ob ||
	current_prog == simul_efun_ob->prog ||
	!apply_low(func, simul_efun_ob, num_arg, 1))
    {
	error ("Simulated efun %s not found", current_prog->rodata + func_name_index);
    }
}

/* ARGSUSED */
static void
f_previous_object(int num_arg)
{
    long long n;
    struct control_stack *cspi;

    if (sp->u.number > 0 || (sp->u.number == 0 &&
			     (previous_ob == 0 || (previous_ob->flags & O_DESTRUCTED))))
    {
	pop_stack();
	push_number(0);
	return;
    }
    else if (sp->u.number == 0)
    {
	pop_stack();
	push_object(previous_ob);
	return;
    }
    n = sp->u.number;
    pop_stack();
    for (cspi = csp; n && cspi > control_stack; cspi--)
	if (cspi->ext_call)
	    n++;
    if (cspi == control_stack || cspi->ob == 0 ||
	(cspi->ob->flags & O_DESTRUCTED))
	push_number(0);
    else
	push_object(cspi->ob);
}

/* ARGSUSED */
static void
f_calling_program(int num_arg)
{
    long long n = sp->u.number;
    struct control_stack *cspi;

    pop_stack();
    if (n > 0 || -n > MAX_TRACE)
    {
	push_number(0);
	return;
    }
    cspi = csp + n;
    if (cspi <= control_stack || cspi->prog == 0)
	push_number(0);
    else
	push_string(cspi->prog->name, STRING_MSTRING);
}

/* ARGSUSED */
static void
f_calling_object(int num_arg)
{ 
    long long n = sp->u.number;
    struct control_stack *cspi;
    
    pop_stack();
    if (n > 0 || -n > MAX_TRACE)
    {
	push_number(0);
	return;
    }
    cspi = csp + n;
    if (cspi <= control_stack || cspi->ob == 0 ||
	(cspi->ob->flags & O_DESTRUCTED))
	push_number(0);
    else
	push_object(cspi->ob);
}

/* ARGSUSED */
static void
f_calling_function(int num_arg)
{
    long long n = sp->u.number;
    struct control_stack *cspi;

    pop_stack();
    if (n > 0 || -n > MAX_TRACE)
    {
	push_number(0);
	return;
    }
    cspi = csp + n - 1;
    if (cspi < control_stack)
	push_number(0);
    else if (cspi->funp)
	push_string(cspi->funp->name, STRING_MSTRING);
    else
	push_string("<internal>", STRING_CSTRING);
}

/* ARGSUSED */
static void
f_store(int num_arg)
{
    fatal("f_store should not be called.\n");
}

/* ARGSUSED */
static void
f_if(int num_arg)
{
    fatal("f_if should not be called.\n");
}

/* ARGSUSED */
static void
f_land(int num_arg)
{
    fatal("f_land should not be called.\n");
}

/* ARGSUSED */
static void
f_lor(int num_arg)
{
    fatal("f_lor should not be called.\n");
}

/* ARGSUSED */
static void
f_status(int num_arg)
{
    fatal("f_status should not be called.\n");
}

/* ARGSUSED */
static void
f_comma(int num_arg)
{
    fatal("f_comma should not be called.\n");
}

/* ARGSUSED */
static void
f_int(int num_arg)
{
    fatal("f_int should not be called.\n");
}

/* ARGSUSED */
static void
f_string_decl(int num_arg)
{
    fatal("f_string_decl should not be called.\n");
}

/* ARGSUSED */
static void
f_else(int num_arg)
{
    fatal("f_else should not be called.\n");
}

/* ARGSUSED */
static void
f_continue(int num_arg)
{
    fatal("f_continue should not be called.\n");
}

/* ARGSUSED */
static void
f_inherit(int num_arg)
{
    fatal("f_inherit should not be called.\n");
}

/* ARGSUSED */
static void
f_colon_colon(int num_arg)
{
    fatal("f_colon_colon should not be called.\n");
}

/* ARGSUSED */
static void
f_static(int num_arg)
{
    fatal("f_static should not be called.\n");
}

/* ARGSUSED */
static void
f_arrow(int num_arg)
{
    fatal("f_arrow should not be called.\n");
}

/* ARGSUSED */
static void
f_object(int num_arg)
{
    fatal("f_object should not be called.\n");
}

/* ARGSUSED */
static void
f_void(int num_arg)
{
    fatal("f_void should not be called.\n");
}

/* ARGSUSED */
static void
f_mixed(int num_arg)
{
    fatal("f_mixed should not be called.\n");
}

/* ARGSUSED */
static void
f_private(int num_arg)
{
    fatal("f_private should not be called.\n");
}

/* ARGSUSED */
static void
f_no_mask(int num_arg)
{
    fatal("f_no_mask should not be called.\n");
}

/* ARGSUSED */
static void
f_mapping(int num_arg)
{
    fatal("f_mapping should not be called.\n");
}

/* ARGSUSED */
static void
f_function(int num_arg)
{
    fatal("f_function should not be called.\n");
}

/* ARGSUSED */
static void
f_operator(int num_arg)
{
    fatal("f_operator should not be called.\n");
}

/* ARGSUSED */
static void
f_float(int num_arg)
{
    fatal("f_float should not be called.\n");
}

/* ARGSUSED */
static void
f_public(int num_arg)
{
    fatal("f_public should not be called.\n");
}

/* ARGSUSED */
static void
f_varargs(int num_arg)
{
    fatal("f_varargs should not be called.\n");
}

/* ARGSUSED */
static void
f_vararg(int num_arg)
{
    fatal("f_vararg should not be called.\n");
}

/* ARGSUSED */
static void
f_case(int num_arg)
{
    fatal("f_case should not be called.\n");
}

/* ARGSUSED */
static void
f_default(int num_arg)
{
    fatal("f_default should not be called.\n");
}

/* ARGSUSED */
static void
f_m_delkey(int num_arg)
{
  remove_from_mapping(sp[-1].u.map, sp);
  pop_n_elems(2);
  push_number(0);
}

/* ARGSUSED */
static void
f_itof(int num_arg)
{
    sp->type = T_FLOAT;
    sp->u.real = sp->u.number;
}

#ifndef LLONG_MAX
#define LLONG_MAX ((long long)(~(unsigned long long)0 >> 1))
#define LLONG_MIN (~LLONG_MAX)
#endif

/* ARGSUSED */
static void
f_ftoi(int num_arg)
{
    if (sp->u.real > LLONG_MAX || sp->u.real < LLONG_MIN)
	error("Integer overflow.\n");
    sp->type = T_NUMBER;
    sp->u.number = sp->u.real;
}

/* ARGSUSED */
static void
f_sin(int num_arg)
{
    /* CHECK result */
    sp->u.real = sin(sp->u.real);
}

/* ARGSUSED */
static void
f_cos(int num_arg)
{
    sp->u.real = cos(sp->u.real);
}

/* ARGSUSED */
static void
f_tan(int num_arg)
{
    sp->u.real = tan(sp->u.real);
}

/* ARGSUSED */
static void
f_asin(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = asin(arg);
    if (errno)
        error("Argument %.18g to asin() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_acos(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = acos(arg);
    if (errno)
        error("Argument %.18g to acos() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_atan(int num_arg)
{
    sp->u.real = atan(sp->u.real);
}

/* ARGSUSED */
static void
f_atan2(int num_arg)
{
    (sp-1)->u.real = atan2((sp-1)->u.real, sp->u.real);
    sp--;
}

/* ARGSUSED */
static void
f_exp(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = exp(arg);
    if (errno)
        error("Argument %.18g to exp() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_log(int num_arg)
{
    sp->u.real = log(sp->u.real);
}

/* ARGSUSED */
static void
f_pow(int num_arg)
{
    double arg1 = (sp-1)->u.real, arg2 = sp->u.real;
    errno = 0;
    (sp-1)->u.real = pow(arg1, arg2);
    if (errno)
        error("Arguments %.18g and %.18g to pow() are out of bounds.\n", arg1, arg2);
    sp--;
}

/* ARGSUSED */
static void
f_sinh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = sinh(arg);
    if (errno)
        error("Argument %.18g to sinh() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_cosh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = cosh(arg);
    if (errno)
        error("Argument %.18g to cosh() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_tanh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = tanh(arg);
    if (errno)
        error("Argument %.18g to tanh() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_asinh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = asinh(arg);
    if (errno)
        error("Argument %.18g to asinh() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_acosh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = acosh(arg);
    if (errno)
        error("Argument %.18g to acosh() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_atanh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = atanh(arg);
    if (errno)
        error("Argument %.18g to atanh() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_abs(int num_arg)
{
    if (sp->type == T_NUMBER)
        sp->u.number = llabs(sp->u.number);
    else if (sp->type == T_FLOAT)
        sp->u.real = fabs(sp->u.real);
    else
        bad_arg(1, F_ABS, sp);
}

/* ARGSUSED */
static void
f_fact(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = signgam * exp(lgamma(sp->u.real + 1.0));
    if (errno)
        error("Argument %.18g to exp() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_rnd(int num_arg)
{
    extern double random_float(int, char *);

    if (num_arg > 0)
    {
	long long seed = sp->u.number;
	pop_stack();
	push_float(random_float(sizeof(seed), (char *)&seed));
    }
    else
	push_float(random_float(0, NULL));
}

/* ARGSUSED */
static void
f_nrnd(int num_arg)
{
    double x1, x2, w, m = 0.0, s = 1.0;
    extern double random_float(int, char *);

    switch (num_arg)
    {
    case 2:
	s = sp->u.real;
	sp--;

    case 1:
	m = sp->u.real;
	sp--;
    }

    do
    {
	x1 = 2.0 * random_float(0, NULL) - 1.0;
	x2 = 2.0 * random_float(0, NULL) - 1.0;
	w = (x1 * x1) + (x2 * x2);
    }
    while (w >= 1.0 || w == 0.0);

    w = sqrt((-2.0 * logl(w)) / w);

    push_float(m + x1 * w * s);
}

/* ARGSUSED */
static void
f_ftoa(int num_arg)
{
    char buffer[1024];

    (void)sprintf(buffer,"%.18g",sp->u.real);
    sp--;
    push_string(buffer, STRING_MSTRING);
}

/* ARGSUSED */
static void
f_floatc(int num_arg)
{
    double f;

    memcpy(&f, pc, sizeof(f));
    pc += sizeof(f);
    push_float(f);
}

/* ARGSUSED */
static void
f_reduce(int argnum)
{
    struct svalue  *argval;

    argval = sp - argnum + 1;

    if (argval[1].type == T_POINTER)
    {
        struct   vector  *arrval;
	struct   svalue   retval;
	register int      arrlen,  number;

	arrval = argval[1].u.vec;
	arrlen = arrval->size;

	if (argnum == 3)
        {
	    push_svalue(&argval[2]);
	    number = -1;
        }
	else if (arrlen)
        {
	    push_svalue(&arrval->item[0]);
	    number = 0;
        }
	else
        {
	    push_svalue(&const0);
	    number = -1;
        }

	while (++number < arrlen)
        {
	    push_svalue(&arrval->item[number]);
	    call_var(2, argval[0].u.func);
        }

	assign_svalue_no_free(&retval, sp);
	pop_n_elems(argnum + 1);
	*(++sp) = retval;
    }
    else
    {
        pop_n_elems(argnum);
	push_svalue(&const0);
    }
}

/* ARGSUSED */
static void
f_regexp(int num_arg)
{
    struct vector *v;

    v = match_regexp((sp-1)->u.vec, sp->u.string);
    pop_n_elems(2);
    if (v == 0)
	push_number(0);
    else {
	push_vector(v, FALSE);
    }
}

/* ARGSUSED */
static void 
f_shadow(int num_arg)
{
    struct object *ob;

    ob = (sp-1)->u.ob;
    if (sp->u.number == 0)
    {
	ob = ob->shadowed;
	pop_n_elems(2);
	push_object(ob);
	return;
    }
    if (validate_shadowing(ob)) 
    {
	/*
	 * The shadow is entered first in the chain.
	 */
	while (ob->shadowed)
	    ob = ob->shadowed;
	change_ref(current_object->shadowing, ob, "f_shadow-1");
	current_object->shadowing = ob;
	change_ref(ob->shadowed, current_object, "f_shadow-2");
	ob->shadowed = current_object;
	pop_n_elems(2);
	push_object(ob);
	return;
    }
    pop_n_elems(2);
    push_number(0);
}

/* ARGSUSED */
static void
f_pop_value(int num_arg)
{
    pop_stack();
}

/* ARGSUSED */
static void
f_dup(int num_arg)
{
    extend_stack();
    assign_svalue_no_free(sp, sp-1);
}

/* ARGSUSED */
static void 
f_jump_when_zero(int num_arg)
{
    unsigned short offset;
    
    ((char *)&offset)[0] = pc[0];
    ((char *)&offset)[1] = pc[1];
    
    if (sp->u.number == 0)
    {
	pc = current_prog->program + offset;
    }
    else
	pc += 2;
    pop_stack();
}

/* ARGSUSED */
static void
f_skip_nz(int num_arg)
{
    unsigned short offset;

    ((char *)&offset)[0] = pc[0];
    ((char *)&offset)[1] = pc[1];
    pc += 2;
    if (sp->type == T_NUMBER && sp->u.number == 0)
	pop_stack();
    else 
	pc = current_prog->program + offset;
}

/* ARGSUSED */
static void 
f_jump(int num_arg)
{
    unsigned short offset;

    ((char *)&offset)[0] = pc[0];
    ((char *)&offset)[1] = pc[1];
    pc = current_prog->program + offset;
}

/* ARGSUSED */
static void 
f_jump_when_non_zero(int num_arg)
{
    unsigned short offset;

    ((char *)&offset)[0] = pc[0];
    ((char *)&offset)[1] = pc[1];
    if (sp->u.number == 0)
	pc += 2;
    else
	pc = current_prog->program + offset;
    pop_stack();
}

/* ARGSUSED */
static void 
f_indirect(int num_arg)
{
#ifdef DEBUG
    if (sp->type != T_LVALUE)
	fatal("Bad type to F_INDIRECT\n");
#endif
    assign_svalue(sp, sp->u.lvalue);
    /*
     * Fetch value of a variable. It is possible that it is a variable
     * that points to a destructed object. In that case, it has to
     * be replaced by 0.
     */
    if ((sp->type == T_OBJECT && (sp->u.ob->flags & O_DESTRUCTED)) ||
	(sp->type == T_FUNCTION && !legal_closure(sp->u.func)))
    {
	free_svalue(sp);
	*sp = const0;
    }
}

/* ARGSUSED */
static void 
f_identifier(int num_arg)
{
    extend_stack();
    assign_svalue_no_free(sp, find_value((int)EXTRACT_UCHAR(pc),
					 (int)EXTRACT_UCHAR(pc + 1)));
    pc += 2;
    /*
     * Fetch value of a variable. It is possible that it is a variable
     * that points to a destructed object. In that case, it has to
     * be replaced by 0.
     */
    if ((sp->type == T_OBJECT && (sp->u.ob->flags & O_DESTRUCTED)) ||
	(sp->type == T_FUNCTION && !legal_closure(sp->u.func)))
	assign_svalue(sp, &const0);
}

/* ARGSUSED */
static void
f_push_identifier_lvalue(int num_arg)
{
    extend_stack();
    sp->type = T_LVALUE;
    sp->u.lvalue = find_value((int)EXTRACT_UCHAR(pc),(int)EXTRACT_UCHAR(pc + 1));
    pc += 2;
}

/* ARGSUSED */
static void
f_push_indexed_lvalue(int num_arg)
{
    push_indexed_lvalue(1);
}

/* ARGSUSED */
static void 
f_index(int num_arg)
{
    push_indexed_lvalue(0);
    assign_svalue_no_free(sp, sp->u.lvalue);
    /*
     * Fetch value of a variable. It is possible that it is a variable
     * that points to a destructed object. In that case, it has to
     * be replaced by 0.
     */
    if ((sp->type == T_OBJECT && (sp->u.ob->flags & O_DESTRUCTED)) ||
	(sp->type == T_FUNCTION && !legal_closure(sp->u.func)))
    {
	free_svalue(sp);
	sp->type = T_NUMBER;
	sp->u.number = 0;
    }
}

/* ARGSUSED */
static void 
f_local_name(int num_arg)
{
    extend_stack();
    assign_svalue_no_free(sp, fp + EXTRACT_UCHAR(pc));
    pc++;
    /*
     * Fetch value of a variable. It is possible that it is a variable
     * that points to a destructed object. In that case, it has to
     * be replaced by 0.
     */
    if ((sp->type == T_OBJECT && (sp->u.ob->flags & O_DESTRUCTED)) ||
	(sp->type == T_FUNCTION && !legal_closure(sp->u.func)))
    {
	free_svalue(sp);
	*sp = const0;
    }
}

/* ARGSUSED */
static void
f_push_local_variable_lvalue(int num_arg)
{
    extend_stack();
    sp->type = T_LVALUE;
    sp->u.lvalue = fp + EXTRACT_UCHAR(pc);
    pc++;
}

/* ARGSUSED */
static void
f_return(int num_arg)
{
    fatal("f_return should not be called.\n");
}

static void 
f_break_string(int num_arg)
{
    struct svalue *arg = sp- num_arg + 1;
    char *str;

    if (arg[0].type == T_STRING)
    {
        str = break_string(arg[0].u.string, arg[1].u.number, 
                           (num_arg > 2 ? &arg[2] : (struct svalue *)0));
        pop_n_elems(num_arg - 1);
        if (str) {
            pop_stack();
            push_mstring(str);
        }
    }
    else
    {
        pop_n_elems(num_arg);
        push_number(0);
    }
}

/* ARGSUSED */
static void 
f_clone_object(int num_arg)
{
    struct object *ob;

    ob = clone_object(sp->u.string);
    pop_stack();
    push_object(ob);
}

/* ARGSUSED */
static void 
f_aggregate(int num_arg)
{
    struct vector *v;
    unsigned short num;
    int i;

    ((char *)&num)[0] = pc[0];
    ((char *)&num)[1] = pc[1];
    pc += 2;
    v = allocate_array((int)num);
    for (i = 0; i < (int)num; i++)
	assign_svalue_no_free(&v->item[i], sp + i - num + 1);
    pop_n_elems((int)num);
    push_vector(v, FALSE);
}

/* ARGSUSED */
static void 
f_m_aggregate(int num_arg)
{
    struct mapping *m;
    unsigned short num;
    struct svalue *arg;
    int i;

    ((char *)&num)[0] = pc[0];
    ((char *)&num)[1] = pc[1];
    pc += 2;
    m = allocate_map((short)num); /* Ref count = 1 */
    for (i = 0 ; i < (int)num ; i += 2)
    {
	arg = sp + i - num;
	assign_svalue(get_map_lvalue(m, arg + 1, 1), arg + 2);
    }
    pop_n_elems((int)num);
    push_mapping(m, FALSE);
}

/* ARGSUSED */
static void
f_tail(int num_arg)
{
    if (tail(sp->u.string))
	assign_svalue(sp, &const1);
    else
	assign_svalue(sp, &const0);
}

/* ARGSUSED */
static void
f_save_map(int num_arg)
{
    save_map(current_object, (sp - 1)->u.map, sp->u.string);
    pop_stack();
}

/* ARGSUSED */
static void
f_save_object(int num_arg)
{
    save_object(current_object, sp->u.string);
    /* The argument is returned */
}

/* ARGSUSED */
static void
f_m_save_object(int num_arg)
{
    push_mapping(m_save_object(current_object), FALSE);
}

/* ARGSUSED */
static void
f_find_object(int num_arg)
{
    struct object *ob;

    ob = find_object2(sp->u.string);
    pop_stack();
    push_object(ob);
}

/* ARGSUSED */
static void
f_wildmatch(int num_arg)
{
    if (sp->type == T_NUMBER)
    {
        pop_n_elems(2);
        push_number(0);
    }
    else
    {
        int  i;
    
        i = wildmat(sp->u.string, (sp-1)->u.string);
        pop_n_elems(2);
        push_number(i);
    }
}

/* ARGSUSED */
static void
f_write_file(int num_arg)
{
    long long i;

    i = write_file((sp-1)->u.string, sp->u.string);
    pop_n_elems(2);
    push_number(i);
}

static void 
f_read_file(int num_arg)
{
    char *str;
    struct svalue *arg = sp- num_arg + 1;
    long long start = 0, len = 0;

    if (num_arg > 1)
	start = arg[1].u.number;
    if (num_arg == 3)
	{
	    if (arg[2].type != T_NUMBER)
		bad_arg(2, F_READ_FILE, &arg[2]);
	    len = arg[2].u.number;
	}

    str = read_file(arg[0].u.string, start, len);
    pop_n_elems(num_arg);
    if (str == 0)
	push_number(0);
    else {
	push_mstring(str);
    }
}

static void 
f_read_bytes(int num_arg)
{
    char *str;
    struct svalue *arg = sp- num_arg + 1;
    long long start = 0;
    long long len = 0;

    if (num_arg > 1)
	start = arg[1].u.number;
    if (num_arg == 3)
    {
	if (arg[2].type != T_NUMBER)
	    bad_arg(2, F_READ_BYTES, &arg[2]);
	len = arg[2].u.number;
    }
	    
    str = read_bytes(arg[0].u.string, start, len);
    pop_n_elems(num_arg);
    if (str == 0)
	push_number(0);
    else
    {
	push_mstring(str);
    }
}

/* ARGSUSED */
static void
f_write_bytes(int num_arg)
{
    int i;

    if (sp->u.string == NULL)
	error("Attempt to write empty string.\n");
    i = write_bytes((sp-2)->u.string, (sp-1)->u.number, sp->u.string);
    pop_n_elems(3);
    push_number(i);
}

/* ARGSUSED */
static void
f_file_size(int num_arg)
{
    long long i;

    i = file_size(sp->u.string);
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_file_time(int num_arg)
{
    long long i;

    i = file_time(sp->u.string);
    pop_stack();
    push_number(i);
}

static void
f_find_living(int num_arg)
{
    extern struct vector *find_living_objects(char *);
    struct object *ob = NULL;
    struct vector *obs = NULL;
    struct svalue *arg;
    
    arg = sp - num_arg + 1;

    if (num_arg == 1 || arg[1].u.number == 0)
	ob = find_living_object(arg[0].u.string);
    else
	obs = find_living_objects(arg[0].u.string);
    pop_n_elems(num_arg);
    if (ob)
	push_object(ob);
    else if (obs)
	push_vector(obs, FALSE);
    else
	push_number(0);
}

/* ARGSUSED */
static void
f_find_player(int num_arg)
{
    extern int num_player;
    struct object *ob;
    int i;

    for (i=0 ; i<num_player ; i++) {
	ob = get_interactive_object(i);
	if (ob->living_name && strcmp(ob->living_name, sp->u.string) == 0) {
	    pop_stack();
	    push_object(ob);
	    return;
	}
    }
    ob = find_living_object(sp->u.string);
    pop_stack();
    if (ob && (ob->flags & O_ONCE_INTERACTIVE))
	push_object(ob);
    else
	push_number(0);
}

/* ARGSUSED */
static void 
f_write_socket(int num_arg)
{
    char tmpbuf[48];
    
    if (sp->type == T_NUMBER)
    {
        snprintf(tmpbuf, sizeof(tmpbuf), "%lld", sp->u.number);
        if (current_object->interactive)
            write_socket(tmpbuf, current_object);
        else if (current_object == master_ob)
            write_socket(tmpbuf, 0);
    }
    else
    {
        if (current_object->interactive)
            write_socket(sp->u.string, current_object);
        else if (current_object == master_ob)
            write_socket(sp->u.string, 0);
    }
}

static void
f_write_socket_gmcp(int num_arg)
{
    const char *json;
    char *str;

    if (current_object->interactive)
    {
        json = val2json(sp);
        str = (char *)xalloc(strlen(json) + strlen((sp - 1)->u.string) + 2);
        strcpy(str, (sp - 1)->u.string); 
        strcat(str, " ");
        strcat(str, json);

        write_gmcp(current_object, (char *)str);
        free(str);
    }

    pop_stack();
    pop_stack();
}

static void
f_val2json(int num_arg)
{
    const char *json;
    json = val2json(sp);

    pop_stack();
    push_string((char *)json, STRING_MSTRING);
}

/* ARGSUSED */
static void
f_str2val(int num_arg)
{
    struct svalue sval = *sp;
    char *str = sval.u.string;

    *sp = const0;
    (void)restore_one(sp, &str);
    free_svalue(&sval);
}

/* ARGSUSED */
static void
f_obsolete(int num_arg)
{
    WARNOBSOLETE(current_object, sp->u.string);
}

/* ARGSUSED */
static void    
f_val2str(int num_arg)
{
    extern char *valtostr(struct svalue *);
    char *ret;

    ret = valtostr(sp);
    pop_stack();
    push_mstring(make_mstring(ret));
    free(ret);
}

/* ARGSUSED */
static void 
f_restore_map(int num_arg)
{
    struct mapping *map = allocate_map(0);
	    
    restore_map(current_object, map, sp->u.string);
    pop_stack();
    push_mapping(map, FALSE);
}

/* ARGSUSED */
static void
f_restore_object(int num_arg)
{
    long long i;

    i = restore_object(current_object, sp->u.string);
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_m_restore_object(int num_arg)
{
    int i;

    i = m_restore_object(current_object, sp->u.map);
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_this_interactive(int num_arg)
{
    if (current_interactive && 
	!(current_interactive->flags & O_DESTRUCTED))
	push_object(current_interactive);
    else
	push_number(0);
}

/* ARGSUSED */
static void
f_this_player(int num_arg)
{
    if (command_giver && !(command_giver->flags & O_DESTRUCTED))
	push_object(command_giver);
    else
	push_number(0);
}

/* ARGSUSED */
static void 
f_set_this_player(int num_arg)
{
    if (sp->type == T_NUMBER)
    {
	if (sp->u.number != 0)
	    error("Bad argument 1 to set_this_player()\n");
	command_giver = 0;
    }
    else
	if (sp->u.ob->flags & O_ENABLE_COMMANDS)
	    command_giver = sp->u.ob;
}

/* ARGSUSED */
static void
f_living(int num_arg)
{
    if (sp->type == T_NUMBER)
    {
	assign_svalue(sp, &const0);
	return;
    }
    if (sp->u.ob->flags & O_ENABLE_COMMANDS)
	assign_svalue(sp, &const1);
    else
	assign_svalue(sp, &const0);
}

/* ARGSUSED */
static void 
f_set_auth(int num_arg)
{
    struct svalue *ret = 0;
    struct svalue *arg = sp - 1;
	    
    if (master_ob)
    {
	push_object(current_object);
	push_object(arg->u.ob);
	push_svalue(arg + 1);
	ret = apply_master_ob(M_VALID_SET_AUTH, 3);
    }
    if (!ret)
    {
	pop_n_elems(2);
	push_number(0);
	return;
    }
    assign_svalue(&(arg->u.ob->variables[-1]), ret);
    pop_n_elems(2);
    push_number(0);
}

/* ARGSUSED */
static void 
f_query_auth(int num_arg)
{
    int i;
    struct object *ob = sp->u.ob;

    pop_stack();
    switch(ob->variables[-1].type)
    {
    case T_POINTER:
	push_vector(allocate_array(ob->variables[-1].u.vec->size), FALSE);
	for(i = 0; i < ob->variables[-1].u.vec->size; i++)
	    assign_svalue_no_free(&(sp->u.vec->item[i]),
				  &(ob->variables[-1].u.vec->item[i]));
	break;
    case T_MAPPING:
	push_mapping(copy_mapping(ob->variables[-1].u.map), FALSE);
	break;
	/* I think functions are handled correctly be default.  -- LA */
    default:
	push_svalue(&(ob->variables[-1]));
	break;
    }
}

/* ARGSUSED */
static void
f_explode(int num_arg)
{
    struct vector *v;

    v = explode_string((sp-1)->u.string, sp->u.string);
    pop_n_elems(2);
    if (v) {
	push_vector(v, FALSE);
    } else {
	push_number(0);
    }
}

static void 
f_filter(int num_arg)
{
    struct svalue *arg;

    arg = sp - num_arg + 1;

    if (num_arg == 2 && arg[1].type == T_FUNCTION) {
	;
    } else if (num_arg >= 3 && arg[1].type == T_STRING) {
	struct closure *fun;
	struct object *ob;

	if (arg[2].type == T_OBJECT)
	    ob = arg[2].u.ob;
	else if (arg[2].type == T_STRING) 
	    ob = find_object(arg[2].u.string);
	else
	    ob = 0;

	if (!ob)
	    bad_arg(3, F_FILTER, &arg[2]);

	/* Fake a function */
	fun = alloc_objclosurestr(FUN_LFUNO, arg[1].u.string, ob, "f_filter", 0);
	if (!fun) {
	    /* We have three choices here:
	     * 1 - return ({}) which is backwards compatible
	     * 2 - return 0 to indicate an error
	     * 3 - generate a runtime error
	     */
#if 0
	    error("Function used in filter could not be found: %s\n", arg[1].u.string);
#else
	    (void)printf("Function used in filter could not be found: %s\n", arg[1].u.string);
	    pop_n_elems(num_arg);
	    push_number(0);
#endif
	    return;
	}
	if (num_arg > 3) {
	    free_vector(fun->funargs);
	    fun->funargs = allocate_array(2);
	    fun->funargs->item[0] = constempty;
	    assign_svalue_no_free(&fun->funargs->item[1], &arg[3]);
	}
	free_svalue(&arg[1]);	/* release old stack location */
	arg[1].type = T_FUNCTION;
	arg[1].u.func = fun;	/* and put in a new one */

	WARNOBSOLETE(current_object, "string as function in filter");
    } else
	error("Bad arguments to filter\n");

    if (arg[0].type == T_POINTER) {
	struct vector *res;
	check_for_destr(&arg[0]);
	res = filter_arr(arg[0].u.vec, arg[1].u.func);
	pop_n_elems(num_arg);
	if (res) {
	    push_vector(res, FALSE);
	} else
	    push_number(0);
    } else if (arg[0].type == T_MAPPING) {
	struct mapping *m;
	check_for_destr(&arg[0]);
	m = filter_map(arg[0].u.map, arg[1].u.func);
	pop_n_elems(num_arg);
	if (m) {
	    push_mapping(m, FALSE);
	} else
	    push_number(0);
    } else {
	/*bad_arg(1, F_FILTER, &arg[0]);*/
	pop_n_elems(num_arg);
	push_number(0);
    }
}

/* ARGSUSED */
static void
f_set_bit(int num_arg)
{
    size_t len, old_len;
    char *str;
    int ind;

    if (sp->u.number > MAX_BITS)
	error("set_bit: too big bit number: %lld\n", sp->u.number);
    if (sp->u.number < 0)
	error("set_bit: negative bit number: %lld\n", sp->u.number);
    len = strlen((sp-1)->u.string);
    old_len = len;
    ind = sp->u.number/6;
    if (ind >= len)
	len = ind + 1;
    str = allocate_mstring(len);
    str[len] = '\0';
    if (old_len)
	(void)memcpy(str, (sp-1)->u.string, old_len);
    if (len > old_len)
	(void)memset(str + old_len, ' ', len - old_len);
    if (str[ind] > 0x3f + ' ' || str[ind] < ' ')
	error("Illegal bit pattern in set_bit character %d\n", ind);
    str[ind] = ((str[ind] - ' ') | (1 << (sp->u.number % 6))) + ' ';
    pop_n_elems(2);
    push_mstring(str);
}

/* ARGSUSED */
static void
f_clear_bit(int num_arg)
{
    size_t len;
    char *str;
    int ind;

    if (sp->u.number > MAX_BITS)
	error("clear_bit: too big bit number: %lld\n", sp->u.number);
    if (sp->u.number < 0)
	error("clear_bit: negative bit number: %lld\n", sp->u.number);
    len = strlen((sp-1)->u.string);
    ind = sp->u.number/6;
    if (ind >= len) {
	/* Return first argument unmodified ! */
	pop_stack();
	return;
    }
    str = allocate_mstring(len);
    (void)memcpy(str, (sp-1)->u.string, len+1);	/* Including null byte */
    if (str[ind] > 0x3f + ' ' || str[ind] < ' ')
	error("Illegal bit pattern in clear_bit character %d\n", ind);
    str[ind] = ((str[ind] - ' ') & ~(1 << sp->u.number % 6)) + ' ';
    pop_n_elems(2);
    push_mstring(str);
}

/* ARGSUSED */
static void
f_test_bit(int num_arg)
{
    int len;

    if (sp->u.number > MAX_BITS)
	error("test_bit: too big bit number: %lld\n", sp->u.number);
    if (sp->u.number < 0)
	error("test_bit: negative bit number: %lld\n", sp->u.number);

    len = strlen((sp-1)->u.string);
    if (sp->u.number/6 >= len) {
	pop_n_elems(2);
	push_number(0);
	return;
    }
    if (((sp-1)->u.string[sp->u.number/6] - ' ') & 1 << sp->u.number % 6) {
	pop_n_elems(2);
	push_number(1);
    } else {
	pop_n_elems(2);
	push_number(0);
    }
}

/* ARGSUSED */
static void
f_try(int num_arg)
{
    fatal("f_try should not be called.\n");
}

/* ARGSUSED */
static void
f_end_try(int num_arg)
{
    fatal("f_end_try should not be called.\n");
}

/* ARGSUSED */
static void
f_catch(int num_arg)
{
    fatal("f_catch should not be called.\n");
}

/* ARGSUSED */
static void
f_end_catch(int num_arg)
{
    fatal("f_end_catch should not be called.\n");
}

/* ARGSUSED */
static void
f_throw(int num_arg)
{
    if (sp->type == T_NUMBER && sp->u.number == 0)
	error("Illegal throw.\n");

    assign_svalue(&catch_value, sp);
    pop_stack();

    throw_error(); /* do the longjump, with extra checks... */
}

/* ARGSUSED */
static void
f_notify_fail(int num_arg)
{
    long long pri = 1;

    if (num_arg == 2)
    {
	pri = sp->u.number;
	pop_stack();
    }
    set_notify_fail_message(sp->u.string, pri);
    /* Return 0 */
    pop_stack();
    push_number(0);
}

/* ARGSUSED */
static void
f_query_idle(int num_arg)
{
    long long i;

    i = query_idle(sp->u.ob);
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_query_interactive(int num_arg)
{
    if (sp->type == T_NUMBER)
    {
	assign_svalue(sp, &const0);
	return;
    }
    assign_svalue(sp, sp->u.ob->interactive ? &const1 : &const0);
}

/* ARGSUSED */
static void
f_implode(int num_arg)
{
    char *str;

    if ((sp-1)->type == T_NUMBER)
    {
	pop_stack();
	return;
    }
    check_for_destr(sp-1);
    str = implode_string((sp-1)->u.vec, sp->u.string);
    pop_n_elems(2);
    if (str) {
	push_mstring(str);
    } else {
	push_number(0);
    }
}

/* ARGSUSED */
static void
f_query_snoop(int num_arg)
{
    struct object *ob;

    if (current_object == master_ob && sp->u.ob->interactive)
	ob = query_snoop(sp->u.ob);
    else
	ob = 0;
    pop_stack();
    push_object(ob);
}


static void
f_query_ip_number_name(int name, int num_arg)
{
    struct svalue *ret;
    char *tmp;
    struct object *ob = command_giver;
	    
    if (num_arg == 1)
    {
	if (sp->type != T_OBJECT)
	    error("Bad optional argument to query_ip_number() or query_ip_name\n");
	else
	    ob = sp->u.ob;
    }

    push_number(name);
    push_object(current_object);
    push_object(ob);
    ret = apply_master_ob(M_VALID_QUERY_IP_NUMBER_NAME, 3);
    if (ret && (ret->type != T_NUMBER || ret->u.number == 0))
    {
	if (num_arg)
	    pop_stack();
	push_number(0);
	return;
    }

    if (name)
	tmp = query_ip_name(num_arg ? sp->u.ob : 0);
    else
	tmp = query_ip_number(num_arg ? sp->u.ob : 0);
    if (num_arg)
	pop_stack();
    if (tmp == 0)
	push_number(0);
    else
	push_string(tmp, STRING_MSTRING);
}

static void
f_query_ip_number(int num_arg)
{
    f_query_ip_number_name(0, num_arg);
}

static void
f_query_ip_name(int num_arg)
{
    f_query_ip_number_name(1, num_arg);
}

/* ARGSUSED */
static void
f_query_ip_ident(int num_arg)
{
    struct object *ob = sp->u.ob;
    struct svalue *ret;
    
    sp--;
    push_object(current_object);
    push_object(ob);
    ret = apply_master_ob(M_VALID_QUERY_IP_IDENT, 2);
    if ((ret && (ret->type != T_NUMBER || ret->u.number == 0)) ||
	!ob->interactive || !ob->interactive->rname)
	push_number(0);
    else
	push_string(ob->interactive->rname, STRING_MSTRING);
    free_object(ob, "f_query_ip_ident");
}

/* ARGSUSED */
static void
f_query_host_name(int num_arg)
{
    extern char *query_host_name(void);
    char *tmp;

    tmp = query_host_name();
    if (tmp)
	push_string(tmp, STRING_MSTRING);
    else
	push_number(0);
}

/* ARGSUSED */
static void
f_all_inventory(int num_arg)
{
    struct vector *vec;

    vec = all_inventory(sp->u.ob);
    pop_stack();
    if (vec == 0) {
	push_number(0);
    } else {
	push_vector(vec, FALSE);
    }
}

/* ARGSUSED */
static void
f_deep_inventory(int num_arg)
{
    struct vector *vec;

    if (sp->type == T_NUMBER)
	vec = allocate_array(0);
    else
	vec = deep_inventory(sp->u.ob, 0);
    free_svalue(sp);
    sp->type = T_POINTER;
    sp->u.vec = vec;
}

/* ARGSUSED */
static void
f_environment(int num_arg)
{
    struct object *ob;

    ob = environment(sp);
    pop_stack();
    push_object(ob);
}

/* ARGSUSED */
static void
f_this_object(int num_arg)
{
    push_object(current_object);
}

/* ARGSUSED */
static void
f_object_clones(int num_arg)
{
    struct vector *v;
    struct object *ob;
    int i;
    int num_clones;
    
    if (sp->type == T_NUMBER)
	v = allocate_array(0);
    else
    {
	ob = sp->u.ob;
	num_clones = sp->u.ob->prog->num_clones;
	v = allocate_array(num_clones);
	for (i = 0, ob = sp->u.ob->prog->clones;
	     i < num_clones;
	     i++, ob = ob->next_all) {
	    v->item[i].type = T_OBJECT;
	    v->item[i].u.ob = ob;
	    add_ref(ob, "object_clones");
	}
    }
    pop_stack();
    push_vector(v, FALSE);
}

/* ARGSUSED */
static void
f_commands(int num_arg)
{
    struct vector *vec;
	    
    if (sp->type == T_NUMBER)
	vec = allocate_array(0);
    else
	vec = get_local_commands(sp->u.ob);
    pop_stack();
    push_vector(vec, FALSE);
}

/* ARGSUSED */
static void
f_typeof(int num_arg)
{
    int   retval;

    retval = sp->type;

    pop_stack();
    push_number(retval);
}

/* ARGSUSED */
static void
f_gettimeofday(int num_arg)
{
    push_float(current_time);
}

/* ARGSUSED */
static void
f_time(int num_arg)
{
    push_number(current_time);
}

/* ARGSUSED */
static void
f_max(int num_arg)
{
    int i;
    struct svalue *arg0, *argn, *maxp;

    arg0 = sp - num_arg + 1;
    argn = arg0 + 1;
    maxp = arg0;

    switch (arg0->type)
    {
    case T_NUMBER:
    case T_STRING:
    case T_FLOAT:
	break;

    default:
	bad_arg(1, F_MAX, arg0);
	break;
    }

    for (i = 1; i < num_arg; i++)
    {
	if (maxp->type != argn->type)
	    bad_arg(i + 1, F_MAX, arg0 + i);

	switch (arg0->type)
	{
	case T_NUMBER:
	    if (maxp->u.number < argn->u.number)
		maxp = argn;
	    break;

	case T_STRING:
	    if (strcmp(maxp->u.string, argn->u.string) < 0)
		maxp = argn;
	    break;

	case T_FLOAT:
	    if (maxp->u.real < argn->u.real)
		maxp = argn;
	    break;
	}

	argn++;
    }

    if (maxp != arg0)
	assign_svalue(arg0, maxp);

    pop_n_elems(num_arg - 1);
}

/* ARGSUSED */
static void
f_min(int num_arg)
{
    int i;
    struct svalue *arg0, *argn, *minp;

    arg0 = sp - num_arg + 1;
    argn = arg0 + 1;
    minp = arg0;

    switch (arg0->type)
    {
    case T_NUMBER:
    case T_STRING:
    case T_FLOAT:
	break;

    default:
	bad_arg(1, F_MIN, arg0);
	break;
    }

    for (i = 1; i < num_arg; i++)
    {
	if (minp->type != argn->type)
	    bad_arg(i + 1, F_MIN, arg0 + i);

	switch (arg0->type)
	{
	case T_NUMBER:
	    if (minp->u.number > argn->u.number)
		minp = argn;
	    break;

	case T_STRING:
	    if (strcmp(minp->u.string, argn->u.string) > 0)
		minp = argn;
	    break;

	case T_FLOAT:
	    if (minp->u.real > argn->u.real)
		minp = argn;
	    break;
	}

	argn++;
    }

    if (minp != arg0)
	assign_svalue(arg0, minp);

    pop_n_elems(num_arg - 1);
}

/* ARGSUSED */
static void
f_add(int num_arg)
{
    /*if (inadd==0) checkplus(p);*/
    if ((sp-1)->type == T_STRING && sp->type == T_STRING)
    {
	char *res;
	int l = strlen((sp-1)->u.string);
	res = allocate_mstring(l + strlen(sp->u.string));
	(void)strcpy(res, (sp-1)->u.string);
	(void)strcpy(res+l, sp->u.string);
	pop_n_elems(2);
	push_mstring(res);
    }
    else if ((sp-1)->type == T_NUMBER && sp->type == T_STRING)
    {
	char buff[60], *res;
	(void)sprintf(buff, "%lld", (sp-1)->u.number);
	res = allocate_mstring(strlen(sp->u.string) + strlen(buff));
	(void)strcpy(res, buff);
	(void)strcat(res, sp->u.string);
	pop_n_elems(2);
	push_mstring(res);
    }
    else if (sp->type == T_NUMBER && (sp-1)->type == T_STRING)
    {
	char buff[60];
	char *res;
	(void)sprintf(buff, "%lld", sp->u.number);
	res = allocate_mstring(strlen((sp-1)->u.string) + strlen(buff));
	(void)strcpy(res, (sp-1)->u.string);
	(void)strcat(res, buff);
	pop_n_elems(2);
	push_mstring(res);
    }
    else if ((sp-1)->type == T_NUMBER && sp->type == T_NUMBER)
    {
	(sp-1)->u.number += sp->u.number;
	sp--;
    }
    else if ((sp-1)->type == T_FLOAT && sp->type == T_FLOAT)
    {
	FLOATASGOP((sp-1)->u.real, += , sp->u.real);
	sp--;
    } 
    else if ((sp-1)->type == T_POINTER && sp->type == T_POINTER)
    {
	struct vector *v;
	check_for_destr(sp-1);
	check_for_destr(sp);
	v = add_array((sp-1)->u.vec,sp->u.vec);
	pop_n_elems(2);
	push_vector(v, FALSE);
    }
    else if ((sp-1)->type == T_MAPPING && sp->type == T_MAPPING)
    {
	struct mapping *m;
	check_for_destr(sp-1);
	check_for_destr(sp);
	m = add_mapping((sp-1)->u.map, sp->u.map);
	pop_n_elems(2);
	push_mapping(m, FALSE);
    }
    else
    {
        bad_arg_op(F_ADD, sp - 1, sp);
    }
}

/* ARGSUSED */
static void
f_subtract(int num_arg)
{

    if ((sp-1)->type == T_POINTER && sp->type == T_POINTER) 
    {
	struct vector *v;

	check_for_destr(sp-1);
	check_for_destr(sp);

	v = subtract_array((sp-1)->u.vec, sp->u.vec);

	pop_stack();
	pop_stack();

	if (v == 0) 
	{
	    push_number(0);
	} 
	else 
	{
	    push_vector(v, FALSE);
	}

	return;
    }
    if ((sp-1)->type == T_FLOAT && sp->type == T_FLOAT) 
    {
	FLOATASGOP((sp-1)->u.real, -= , sp->u.real);
	sp--;
	return;
    }
    if ((sp-1)->type != T_NUMBER ||
        sp->type != T_NUMBER)
        bad_arg_op(F_SUBTRACT, sp - 1, sp);
    (sp-1)->u.number -= sp->u.number;
    sp--;
}

/* ARGSUSED */
static void
f_and(int num_arg)
{
    if (sp->type == T_POINTER && (sp-1)->type == T_POINTER)
    {
	struct vector *v;

	v = intersect_array(sp->u.vec, (sp-1)->u.vec);

	pop_stack();
	pop_stack();
	if (v == 0) 
	{
	    push_number(0);
	} 
	else 
	{
	    push_vector(v, FALSE);
	}
	return;
    }
    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_AND, sp - 1, sp);
    (sp-1)->u.number &= sp->u.number;
    sp--;
}

/* ARGSUSED */
static void
f_or(int num_arg)
{
    if (sp->type == T_POINTER && (sp-1)->type == T_POINTER)
    {
	struct vector *v;

	v = union_array((sp-1)->u.vec, sp->u.vec);

	pop_stack();
	pop_stack();

	if (v == NULL) 
	    push_number(0);
	else 
	    push_vector(v, FALSE);

	return;
    }

    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
	bad_arg_op(F_OR, sp-1, sp);
    (sp-1)->u.number |= sp->u.number;
    sp--;
}

/* ARGSUSED */
static void
f_xor(int num_arg)
{
    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_XOR, sp-1, sp);
    (sp-1)->u.number ^= sp->u.number;
    sp--;
}

/* ARGSUSED */
static void
f_lsh(int num_arg)
{
    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_LSH, sp-1, sp);
    (sp-1)->u.number <<= sp->u.number;
    sp--;
}

/* ARGSUSED */
static void
f_rsh(int num_arg)
{
    long long i;

    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_RSH, sp-1, sp);
    i = (long long)((unsigned long long)(sp-1)->u.number >> sp->u.number);
    sp--;	
    sp->u.number = i;
}

/* ARGSUSED */
static void
f_multiply(int num_arg)
{
    if ((sp-1)->type == T_FLOAT && sp->type == T_FLOAT)
    {
	FLOATASGOP((sp-1)->u.real, *= , sp->u.real);
	sp--;
	return;
    }
    if ((sp-1)->type == T_NUMBER) {
	if (sp->type == T_NUMBER) {
            (sp-1)->u.number *= sp->u.number;
            sp--;
            return;
        }
        else if (sp->type == T_STRING) {
            char *result = multiply_string(sp->u.string, (sp-1)->u.number);
	    pop_stack();
	    pop_stack();
	    push_string(result, STRING_MSTRING);
	    return;
        }
        else if (sp->type == T_POINTER) {
	    struct vector *result = multiply_array(sp->u.vec,
						   (sp-1)->u.number);
	    pop_stack();
	    pop_stack();
	    push_vector(result, 0);
            return;
        }
    } else if ((sp-1)->type == T_POINTER &&
	       sp->type == T_NUMBER) {
	    struct vector *result = multiply_array((sp-1)->u.vec,
						   sp->u.number);
	    pop_stack();
	    pop_stack();
	    push_vector(result, 0);
            return;
   } else if ((sp-1)->type == T_STRING &&
	       sp->type == T_NUMBER) {
	char *result = multiply_string((sp-1)->u.string, sp->u.number);
	pop_stack();
	pop_stack();
	push_string(result, STRING_MSTRING);
	return;
    }
    bad_arg_op(F_MULTIPLY, sp-1, sp);
}

/* ARGSUSED */
static void
f_divide(int num_arg)
{
    if ((sp-1)->type == T_FLOAT && sp->type == T_FLOAT)
    {
        if (sp->u.real == 0.0)
	    error("Division by zero\n");
	FLOATASGOP((sp-1)->u.real, /= , sp->u.real);
	sp--;
	return;
    }
    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_DIVIDE, sp-1, sp);
    if (sp->u.number == 0)
	error("Division by zero\n");
    (sp-1)->u.number /= sp->u.number;
    sp--;
}

/* ARGSUSED */
static void
f_mod(int num_arg)
{
    if ((sp-1)->type != sp->type ||
        (sp->type != T_NUMBER && sp->type != T_FLOAT))
      bad_arg_op(F_MOD, sp-1, sp);

    if (sp->type == T_NUMBER) {
      if (sp->u.number == 0)
	error("Modulus by zero.\n");
      (sp-1)->u.number %= sp->u.number;
    } else if (sp->type == T_FLOAT) {
      errno = 0;
      (sp-1)->u.real = fmod((sp-1)->u.real, sp->u.real);
      if (errno)
	error("Modulus by zero.\n");
    } 
    sp--;
}

/* ARGSUSED */
static void
f_gt(int num_arg)
{
    long long i;

    if ((sp-1)->type == T_STRING && sp->type == T_STRING) {
	i = strcmp((sp-1)->u.string, sp->u.string) > 0;
	pop_n_elems(2);
	push_number(i);
	return;
    }
    if ((sp-1)->type == T_FLOAT && sp->type == T_FLOAT)
    {
	(sp-1)->u.number = (sp-1)->u.real > sp->u.real;
	sp--;
	sp->type = T_NUMBER;
	return;
    }
    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_GT, sp-1, sp);
    i = (sp-1)->u.number > sp->u.number;
    sp--;
    sp->u.number = i;
}

/* ARGSUSED */
static void
f_ge(int num_arg)
{
    long long i;

    if ((sp-1)->type == T_STRING && sp->type == T_STRING) {
	i = strcmp((sp-1)->u.string, sp->u.string) >= 0;
	pop_n_elems(2);
	push_number(i);
	return;
    }
    if ((sp-1)->type == T_FLOAT && sp->type == T_FLOAT)
    {
	(sp-1)->u.number = (sp-1)->u.real >= sp->u.real;
	sp--;
	sp->type = T_NUMBER;
	return;
    }
    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_GE, sp-1, sp);
    i = (sp-1)->u.number >= sp->u.number;
    sp--;
    sp->u.number = i;
}

/* ARGSUSED */
static void
f_lt(int num_arg)
{
    long long i;

    if ((sp-1)->type == T_STRING && sp->type == T_STRING) {
	i = strcmp((sp-1)->u.string, sp->u.string) < 0;
	pop_n_elems(2);
	push_number(i);
	return;
    }
    if ((sp-1)->type == T_FLOAT && sp->type == T_FLOAT)
    {
	(sp-1)->u.number = (sp-1)->u.real < sp->u.real;
	sp--;
	sp->type = T_NUMBER;
	return;
    }
    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_LT, sp-1, sp);
    i = (sp-1)->u.number < sp->u.number;
    sp--;
    sp->u.number = i;
}

/* ARGSUSED */
static void
f_le(int num_arg)
{
    long long i;

    if ((sp-1)->type == T_STRING && sp->type == T_STRING) {
	i = strcmp((sp-1)->u.string, sp->u.string) <= 0;
	pop_n_elems(2);
	push_number(i);
	return;
    }
    if ((sp-1)->type == T_FLOAT && sp->type == T_FLOAT)
    {
	(sp-1)->u.number = (sp-1)->u.real <= sp->u.real;
	sp--;
	sp->type = T_NUMBER;
	return;
    }
    if ((sp-1)->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_LE, sp-1, sp);

    i = (sp-1)->u.number <= sp->u.number;
    pop_stack();
    sp->u.number = i;
}

/* ARGSUSED */
static void
f_eq(int num_arg)
{
    int i;

    i = equal_svalue(sp-1, sp);
    pop_stack();
    assign_svalue(sp, i ? &const1 : &const0);
}

/* ARGSUSED */
static void
f_ne(int num_arg)
{
    int i;

    i = equal_svalue(sp-1, sp);
    pop_stack();
    assign_svalue(sp, i ? &const0 : &const1);
}

/* ARGSUSED */
static void
f_not(int num_arg)
{
    if ((sp->type == T_NUMBER && sp->u.number == 0) ||
	(sp->type == T_FUNCTION && !legal_closure(sp->u.func)) ||
	(sp->type == T_OBJECT && (sp->u.ob->flags & O_DESTRUCTED)))
	assign_svalue(sp, &const1);
    else
	assign_svalue(sp, &const0);
}

/* ARGSUSED */
static void
f_compl(int num_arg)
{
    if (sp->type != T_NUMBER)
      bad_arg(1, F_COMPL, sp);
    sp->u.number = ~ sp->u.number;
}

/* ARGSUSED */
static void
f_negate(int num_arg)
{
    if (sp->type == T_FLOAT)
    {
	sp->u.real = - sp->u.real;
	return;
    }
    if (sp->type != T_NUMBER)
      bad_arg(1, F_NEGATE, sp);
    sp->u.number = - sp->u.number;
}

/* ARGSUSED */
static void
f_inc(int num_arg)
{
    if (sp->type != T_LVALUE)
	error("Non-lvalue argument to ++\n");
    if (sp->u.lvalue->type != T_NUMBER)
      bad_arg(1, F_INC, sp->u.lvalue);
    sp->u.lvalue->u.number++;
    assign_svalue(sp, sp->u.lvalue);
}

/* ARGSUSED */
static void
f_dec(int num_arg)
{
    if (sp->type != T_LVALUE)
	error("Non-lvalue argument to --\n");
    if (sp->u.lvalue->type != T_NUMBER)
      bad_arg(1, F_DEC, sp->u.lvalue);
    sp->u.lvalue->u.number--;
    assign_svalue(sp, sp->u.lvalue);
}

/* ARGSUSED */
static void
f_post_inc(int num_arg)
{
    if (sp->type != T_LVALUE)
	error("Non-lvalue argument to ++\n");
    if (sp->u.lvalue->type != T_NUMBER)
      bad_arg(1, F_POST_INC, sp->u.lvalue);
    sp->u.lvalue->u.number++;
    assign_svalue(sp, sp->u.lvalue);
    sp->u.number--;
}

/* ARGSUSED */
static void
f_post_dec(int num_arg)
{
    if (sp->type != T_LVALUE)
	error("Non-lvalue argument to --\n");
    if (sp->u.lvalue->type != T_NUMBER)
      bad_arg(1, F_POST_DEC, sp->u.lvalue);
    sp->u.lvalue->u.number--;
    assign_svalue(sp, sp->u.lvalue);
    sp->u.number++;
}

static void 
f_call_other(int num_arg)
{
    struct object *ob;
    struct svalue *arg, *svp;

#ifdef COUNT_CALLS
    num_call_other++;
#endif	
    arg = sp - num_arg + 1;
    if (arg[0].type == T_NUMBER)
    {
	if (arg[0].u.number != 0)
	  bad_arg(1, F_CALL_OTHER, arg);
	pop_n_elems(num_arg);
	push_number(0);
	return;
    }
    if (arg[0].type == T_POINTER)
    {
	struct vector *w, *v = allocate_array(num_arg - 2);
	int i, j;

	for (i = 0; i < num_arg - 2; i++)
	    assign_svalue_no_free(&v->item[i], &arg[i + 2]);
	pop_n_elems(num_arg - 2);
	w = allocate_array(arg[0].u.vec->size);
	for (i = 0; i < arg[0].u.vec->size; i++)
	{
	    if (arg[0].u.vec->item[i].type != T_OBJECT &&
		arg[0].u.vec->item[i].type != T_STRING)
		continue;
	    if (arg[0].u.vec->item[i].type == T_OBJECT)
		ob = arg[0].u.vec->item[i].u.ob;
	    else
		ob = find_object(arg[0].u.vec->item[i].u.string);
	    if (!ob || (ob->flags & O_DESTRUCTED))
		continue;
	    for (j = 0; j < v->size; j++)
		push_svalue(&v->item[j]);
#ifdef TRACE_CODE
	    if (TRACEP(TRACE_CALL_OTHER)) 
	    {
		char buff[1024];
		(void)sprintf(buff,"%s->%s", ob->name,arg[1].u.string);
		do_trace("Call other ", buff, "\n");
	    }
#endif
	    if (apply_low(arg[1].u.string, ob, v->size, 1) == 0)
		continue;	/* function not found */
	    w->item[i] = *sp--;
	}
	
	pop_n_elems(2);
	push_vector(w, FALSE);
	free_vector(v);
	return;
    }
    if (arg[0].type == T_MAPPING)
    {
	struct vector *w, *v = allocate_array(num_arg - 2);
	struct vector *ix, *o;
	int i, j;

	ix = map_domain(arg[0].u.map);
	o = map_codomain(arg[0].u.map);
	for (i = 0; i < num_arg - 2; i++)
	    assign_svalue_no_free(&v->item[i], &arg[i + 2]);
	pop_n_elems(num_arg - 2);
	w = allocate_array(o->size);
	for (i = 0; i < o->size; i++)
	{
	    if (o->item[i].type != T_OBJECT &&
		o->item[i].type != T_STRING)
		continue;
	    if (o->item[i].type == T_OBJECT)
		ob = o->item[i].u.ob;
	    else
		ob = find_object(o->item[i].u.string);
	    if (!ob || (ob->flags & O_DESTRUCTED))
		continue;
	    for (j = 0; j < v->size; j++)
		push_svalue(&v->item[j]);
#ifdef TRACE_CODE
	    if (TRACEP(TRACE_CALL_OTHER)) 
	    {
		char buff[1024];
		(void)sprintf(buff,"%s->%s", ob->name, arg[1].u.string);
		do_trace("Call other ", buff, "\n");
	    }
#endif
	    if (apply_low(arg[1].u.string, ob, v->size, 1) == 0)
		continue;	/* function not found */
	    w->item[i] = *sp--;
	}
	
	pop_n_elems(2);
	push_mapping(make_mapping(ix,w), FALSE);
	free_vector(o);
	free_vector(ix);
	free_vector(v);
	free_vector(w);
	return;
    }
    if (arg[0].type == T_OBJECT)
	ob = arg[0].u.ob;
    else 
    {
	ob = find_object(arg[0].u.string);
	if (ob == 0)
	    error("call_other() failed\n");
    }
    if (current_object->flags & O_DESTRUCTED) 
    {
	/*
	 * No external calls may be done when this object is
	 * destructed.
	 */
	pop_n_elems(num_arg);
	push_number(0);
	return;
    }
    /*
     * Send the remaining arguments to the function.
     */
#ifdef TRACE_CODE
    if (TRACEP(TRACE_CALL_OTHER)) 
    {
	char buff[1024];
	(void)sprintf(buff,"%s->%s", ob->name, arg[1].u.string);
	do_trace("Call other ", buff, "\n");
    }
#endif
        
    if (apply_low(arg[1].u.string, ob, num_arg - 2, 1) == 0) 
    {
	/* Function not found */
	pop_n_elems(2);
	push_number(0);
	return;
    }
    /*
     * The result of the function call is on the stack. But, so
     * is the function name and object that was called.
     * These have to be removed.
     */
    svp = sp--;			/* Copy the function call result */
    pop_n_elems(2);		/* Remove old arguments to call_other */
    *++sp = *svp;		/* Re-insert function result */
}

/* ARGSUSED */
static void
f_call_otherv(int xxx)
{
    struct vector *argv = sp->u.vec;
    int i;
    int num_arg;

    INCREF(argv->ref);
    pop_stack();
    num_arg = argv->size + 2;
    for(i = 0; i < argv->size; i++)
    {
	push_svalue(&argv->item[i]);
    }
    free_vector(argv);
    f_call_other(num_arg);
}

/* ARGSUSED */
static void
f_object_time(int num_arg)
{
    long long i;

    if (sp->type == T_OBJECT)
    {
	i = sp->u.ob->created;
	pop_stack();
	push_number(i);
    }
    else
	assign_svalue(sp, &const0);
}

/* ARGSUSED */
static void
f_intp(int num_arg)
{
    assign_svalue(sp, sp->type == T_NUMBER ? &const1 : &const0);
}

/* ARGSUSED */
static void
f_stringp(int num_arg)
{
    assign_svalue(sp, sp->type == T_STRING ? &const1 : &const0);
}

/* ARGSUSED */
static void
f_objectp(int num_arg)
{
    assign_svalue(sp, sp->type == T_OBJECT ? &const1 : &const0);
}

/* ARGSUSED */
static void
f_pointerp(int num_arg)
{
    assign_svalue(sp, sp->type == T_POINTER ? &const1 : &const0);
}

/* ARGSUSED */
static void
f_mappingp(int num_arg)
{
    assign_svalue(sp, sp->type == T_MAPPING ? &const1 : &const0);
}

/* ARGSUSED */
static void
f_floatp(int num_arg)
{
    assign_svalue(sp, sp->type == T_FLOAT ? &const1 : &const0);
}

/* ARGSUSED */
static void
f_function_object(int num_arg)
{
    struct object *ob;

    ob = sp->u.func->funobj;
    pop_stack();
    push_object(ob);
}

/* ARGSUSED */
static void
f_function_name(int num_arg)
{
    char *p;

    if (!legal_closure(sp->u.func))
	assign_svalue(sp, &const0);
    else
    {
	p = make_mstring(show_closure(sp->u.func));
	pop_stack();
	push_mstring(p);
    }
}

/* ARGSUSED */
static void
f_functionp(int num_arg)
{
    int ret;

    ret = 0;
    if (sp->type == T_FUNCTION)
	ret = legal_closure(sp->u.func);
    assign_svalue(sp, ret ? &const1 : &const0);
}

static void
f_extract(int num_arg)
{
    long long len, from, to;
    struct svalue *arg;
    char *res;
	    
    arg = sp - num_arg + 1;
    len = strlen(arg[0].u.string);
    if (num_arg == 1)
	return;			/* Simply return argument */
    from = arg[1].u.number;
    if (from < 0)
	from = len + from;
    if (from >= len) {
	pop_n_elems(num_arg);
	push_string("", STRING_CSTRING);
	return;
    }
    if (num_arg == 2) {
	res = make_mstring(arg->u.string + from);
	pop_n_elems(2);
	push_mstring(res);
	return;
    }
    if (arg[2].type != T_NUMBER)
	error("Bad third argument to extract()\n");
    to = arg[2].u.number;
    if (to < 0)
	to = len + to;
    if (to < from) {
	pop_n_elems(3);
	push_string("", STRING_CSTRING);
	return;
    }
    if (to >= len)
	to = len-1;
    if (to == len-1) {
	res = make_mstring(arg->u.string + from);
	pop_n_elems(3);
	push_mstring(res);
	return;
    }
    res = allocate_mstring((size_t)(to - from + 1));
    (void)strncpy(res, arg[0].u.string + from, (size_t)(to - from + 1));
    res[to - from + 1] = '\0';
    pop_n_elems(3);
    push_mstring(res);
}

/* ARGSUSED */
static void
f_range(int num_arg)
{
    if (sp[-1].type != T_NUMBER)
      bad_arg(2, F_RANGE, sp - 1);
    if (sp[0].type != T_NUMBER)
      bad_arg(3, F_RANGE, sp);
    if (sp[-2].type == T_POINTER) {
	struct vector *v;

	v = slice_array(sp[-2].u.vec, sp[-1].u.number, sp[0].u.number);
	pop_n_elems(3);
	if (v) {
	    push_vector(v, FALSE);
	} else {
	    push_number(0);
	}
    } else if (sp[-2].type == T_STRING) {
	int len, from, to;
	char *res;

	len = strlen(sp[-2].u.string);
	from = sp[-1].u.number;
	if (from < 0)
	    from = len + from;
	if (from < 0)
	    from = 0;
	if (from >= len) {
	    pop_n_elems(3);
	    push_string("", STRING_CSTRING);
	    return;
	}
	to = sp[0].u.number;
	if (to < 0)
	    to = len + to;
	if (to < from) {
	    pop_n_elems(3);
	    push_string("", STRING_CSTRING);
	    return;
	}
	if (to >= len)
	    to = len - 1;
	if (to == len - 1) {
	    res = make_mstring(sp[-2].u.string + from);
	    pop_n_elems(3);
	    push_mstring(res);
	    return;
	}
	res = allocate_mstring((size_t)(to - from + 1));
	(void)strncpy(res, sp[-2].u.string + from, (size_t)(to - from + 1));
	res[to - from + 1] = '\0';
	pop_n_elems(3);
	push_mstring(res);
    }
    else
    {
      bad_arg(1, F_RANGE, sp - 2);
    }
}

/* ARGSUSED */
static void
f_query_verb(int num_arg)
{
    if (last_verb == 0) {
	push_number(0);
	return;
    }
    push_string(last_verb, STRING_MSTRING);
}

/* ARGSUSED */
static void
f_query_trigverb(int num_arg)
{
    if (trig_verb == 0) {
	push_number(0);
	return;
    }
    push_string(trig_verb, STRING_MSTRING);
}

/* ARGSUSED */
static void
f_exec(int num_arg)
{
    int i;

    if ((sp-1)->type == T_NUMBER)
	i = replace_interactive(0, sp->u.ob, current_prog->name);
    else
	i = replace_interactive((sp-1)->u.ob, sp->u.ob,
				current_prog->name);
    pop_stack();
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_file_name(int num_arg)
{
    char *name,*res;
	    
    /* This function now returns a leading '/', except when -o flag */
    name = sp->u.ob->name;
    res = add_slash(name);
    pop_stack();
    push_mstring(res);
}

/* ARGSUSED */
static void
f_users(int num_arg)
{
    struct svalue *ret;

    if (current_object != master_ob)
    {
	push_object(current_object);
	ret = apply_master_ob(M_VALID_USERS, 1);
	if (ret && (ret->type != T_NUMBER || ret->u.number == 0))
	{
	    push_number(0);
	    return;
	}
    }
    push_vector(users(), FALSE);
}

static void
f_set_alarm(int num_arg)
{
    struct svalue *arg = sp - num_arg + 1;
    double delay, reload;
    long long ret;
    struct closure *f;
    struct object *ob;

    if (num_arg == 3 && arg[2].type == T_FUNCTION) {
	;
    } else if (num_arg >= 3 && arg[2].type == T_STRING) {
	struct closure *fun;

	if (*(arg[2].u.string) == '.') {
	    pop_n_elems(num_arg);
	    push_number(0);
	    return;
	}

	/* Fake a function */
	fun = alloc_objclosurestr(FUN_LFUNO, arg[2].u.string, current_object, "f_set_alarm", 0);
	if (!fun) {
	    pop_n_elems(num_arg);
	    push_number(0);
	    return;
	}
	free_vector(fun->funargs);
	fun->funargs = allocate_array(num_arg - 3);
	(void)memcpy(&fun->funargs->item[0], &arg[3], (num_arg - 3) * sizeof(struct svalue));

	sp = &arg[2];
	free_svalue(&arg[2]);	/* release old stack location */
	arg[2].type = T_FUNCTION;
	arg[2].u.func = fun;	/* and put in a new one */

	WARNOBSOLETE(current_object, "string as function in set_alarm");
    } else
      bad_arg(3, F_SET_ALARM, arg + 2);

    f = arg[2].u.func;
    ob = f->funobj;
    if (f->funtype == FUN_EFUN || !ob)
	error("set_alarm can only be called with a local function.");

    if (!(ob->flags & O_DESTRUCTED)) {
        delay = 0.0;
        reload = -1.0;
        if (arg[0].u.real >= 0.0)
	    delay = arg[0].u.real;
        if (arg[1].u.real > 0.0)
	    reload = arg[1].u.real;
	ret = new_call_out(f, delay, reload);
    } else
	ret = 0;
    pop_n_elems(3);
    push_number(ret);
}

/* ARGSUSED */
static void
f_set_alarmv(int xxx)
{
    int num_arg = 4;
    struct svalue *arg = sp - num_arg + 1;
    double delay, reload;
    long long ret;
    struct closure *f;

    WARNOBSOLETE(current_object, "set_alarmv");

    if (arg[2].type != T_STRING)
      bad_arg(3, F_SET_ALARMV, arg + 2);
    if (arg[3].type != T_POINTER)
      bad_arg(4, F_SET_ALARMV, arg + 3);

    if (*(arg[2].u.string) == '.')
    {
	pop_n_elems(num_arg);
	push_number(0);
	return;
    }

    {
	struct closure *fun;

	/* Fake a function */
	fun = alloc_objclosurestr(FUN_LFUNO, arg[2].u.string, current_object, "f_set_alarmv", 0);
	if (!fun) {
	    pop_n_elems(num_arg);
	    push_number(0);
	    return;
	}
	free_vector(fun->funargs);
	fun->funargs = arg[3].u.vec;
	INCREF(fun->funargs->ref);

	free_svalue(&arg[2]);	/* release old stack location */
	arg[2].type = T_FUNCTION;
	arg[2].u.func = fun;	/* and put in a new one */
    }

    f = arg[2].u.func;

    if (legal_closure(f))
    {
        delay = 0.0;
        reload = -1.0;
        if (arg[0].u.real >= 0.0)
	    delay = arg[0].u.real;
        if (arg[1].u.real > 0.0)
	    reload = arg[1].u.real;
	ret = new_call_out(f, delay, reload);
    } else
	ret = 0;
    pop_n_elems(num_arg);
    push_number(ret);
}

/* ARGSUSED */
static void
f_remove_alarm(int num_arg)
{
    extern void delete_call(struct object *, long long);
    
    delete_call(current_object, sp->u.number);
}

/* ARGSUSED */
static void
f_get_all_alarms(int num_arg)
{
    struct vector *ret;
    extern struct vector *get_calls(struct object *);
    ret = get_calls(current_object);
    if (ret)
    {
	push_vector(ret, FALSE);
    }
    else
	push_number(0);
}

/* ARGSUSED */
static void
f_get_alarm(int num_arg)
{
    struct vector *ret;
    extern struct vector *get_call(struct object *, long long);
    ret = get_call(current_object, sp->u.number);
    pop_stack();
    if (ret)
    {
	push_vector(ret, FALSE);
    }
    else
	push_number(0);
}	


#ifdef WORD_WRAP
/* ARGSUSED */
static void
f_set_screen_width(int num_arg)
{
    long long col;

    if (! current_object->interactive)
	return;
    col = sp->u.number;
    if (col < 0 || col == 1|| col > (1 << 30))
	error("Nonsensical screen width\n");
    current_object->interactive->screen_width = col;
    if (current_object->interactive->current_column >=
	col)
	current_object->interactive->current_column = col - 1;

    /* Return first argument */
}

/* ARGSUSED */
static void
f_query_screen_width(int num_arg)
{
    int i;

    i = -1;
    if (current_object->interactive)
	i = current_object->interactive->screen_width;
    push_number(i);
}
#endif

static void
f_sprintf(int num_arg)
{
    char *s;

    /*
     * string_print_formatted() returns a pointer to it's internal
     * buffer, or to an internal constant...  Either way, it must
     * be copied before it's returned as a string.
     */

    s = string_print_formatted(1, (sp-num_arg+1)->u.string,
			       num_arg-1, sp-num_arg+2);
    pop_n_elems(num_arg);
    if (s == NULL) 
	push_number(0); 
    else 
	push_string(s, STRING_MSTRING);
}

/* ARGSUSED */
static void
f_member_array(int num_arg)
{
    struct vector *v;
    int		  i;

    if (sp->type == T_NUMBER)
    {
	pop_n_elems(2);
	push_number(-1);
	return;
    }
    v = sp->u.vec;
    check_for_destr(sp);
    for (i=0; i < v->size; i++) {
	if (equal_svalue(&v->item[i], sp - 1))
	    break;
    }
    if (i == v->size)
	i = -1;			/* Return -1 for failure */
    pop_n_elems(2);
    push_number(i);
}

static void
f_move_object(int num_arg)
{
    struct object *ob;
    struct svalue *ret;

    if (sp->type == T_OBJECT)
	ob = sp->u.ob;
    else
	ob = find_object(sp->u.string);
    push_object(current_object);
    push_object(ob);
    ret = apply_master_ob(M_VALID_MOVE, 2);
    if (ret && (ret->type != T_NUMBER || ret->u.number == 0))
    {
	pop_n_elems(num_arg);	/* Get rid of all arguments */
	push_number(0);
	return;
    }
    move_object(ob);
}

/* ARGSUSED */
static void
f_update_actions(int num_arg)
{
    update_actions(current_object);
}

/* ARGSUSED */
static void
f_function_exists(int num_arg)
{
    char *str, *res;
    size_t len;

    str = function_exists((sp-1)->u.string, sp->u.ob);
    pop_n_elems(2);
    if (str) {
	len = strlen(str);
	res = strrchr(str, '.');
	if (res != NULL)
	    len = res - str;
	res = allocate_mstring(len + 1);
	res[0] = '/';
	(void)memcpy(&res[1], str, len);
	res[len + 1] = '\0';
	push_mstring(res);
    } else {
	push_number(0);
    }
}

static void
f_snoop(int num_arg)
{
    struct object *ob;

    /* 
     * This one takes a variable number of arguments. It returns
     * 0 or an object.
     */
    if (!command_giver) 
    {
	pop_n_elems(num_arg);
	push_number(0);
    } 
    else 
    {
	ob = 0;			/* Do not remove this, it is not 0 by default */
	switch (num_arg) {
	    case 1:
		if (set_snoop(sp->u.ob, 0))
		    ob = sp->u.ob;
		break;
	    case 2:
		if (set_snoop((sp-1)->u.ob, sp->u.ob))
		    ob = sp->u.ob;
		break;
	    default:
		ob = 0;
		break;
	}
	pop_n_elems(num_arg);
	push_object(ob);
    }
}

static void
f_add_action(int num_arg)
{
    struct svalue *arg = sp - num_arg + 1;
    int flag;

    if (num_arg == 3) {
	if (arg[2].type != T_NUMBER)
	    bad_arg(3, F_ADD_ACTION, arg + 2);
    }
    if (arg[0].type == T_FUNCTION) {
	;
    } else if (arg[0].type == T_STRING) {
	struct closure *fun;

	WARNOBSOLETE(current_object, "string function in add_action");

	fun = alloc_objclosurestr(FUN_LFUNO, arg[0].u.string, current_object, "f_add_action", 1);
	if (!fun) {
	    (void)fprintf(stderr, "add_action failed: %s(verb=%s) in %s\n",
			  arg[0].u.string, arg[1].u.string, current_object->name);
	    pop_n_elems(num_arg);
	    push_number(0);	/* XXX error? */
	    return;
	}
	free_svalue(&arg[0]);	/* release old stack location */
	arg[0].type = T_FUNCTION;
	arg[0].u.func = fun;	/* and put in a new one */
    } else
	bad_arg(1, F_ADD_ACTION, arg);

    flag = 0;
    if (num_arg > 2) {
        if (arg[2].u.number == V_NO_SPACE)
           flag = V_NO_SPACE;
        else if (arg[2].u.number != 0)
           flag = 1;
    }
    add_action(arg[0].u.func,
	       num_arg > 1 ? &arg[1] : (struct svalue *)0, flag);
    pop_n_elems(num_arg - 1);
}

/* ARGSUSED */
static void
f_allocate(int num_arg)
{
    struct vector *v;

    v = allocate_array(sp->u.number); /* Will have ref count == 1 */
    pop_stack();
    push_vector(v, FALSE);
}

static void
f_ed(int num_arg)
{
    if (num_arg == 0) {
	push_number(0);
    } else if (num_arg == 1) {
	ed_start(sp->u.string, 0);
    } else if (num_arg == 2 && sp->type == T_FUNCTION) {
	struct closure *f = sp->u.func;
	INCREF(f->ref);
	ed_start((sp-1)->u.string, f);
	pop_stack();		/* remove function from stack */
    } else if (num_arg == 2 && sp->type == T_STRING) {
	struct closure *f = alloc_objclosurestr(FUN_LFUNO, sp->u.string, current_object, "f_ed", 1);
	WARNOBSOLETE(current_object, "string function in ed");
	if (!f) {
	    pop_n_elems(num_arg);
	    push_number(0);
	    return;
	}
	ed_start((sp-1)->u.string, f);
	pop_stack();
    } else 
	error("Bad arg to ed()\n");
}

static char *
build_salt(int length)
{
    char *str;
    int i;
    static char *choise = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    
    str = xalloc(length + 1);    
    for (i = 0; i < length; i++)
        str[i] = choise[random_number((int)strlen(choise), 0, NULL)];

    if (length > 0)
        str[length] = '\0';
    
    return str;
}

#define MAX_SALT_LENGTH 16

/* ARGSUSED */
static void
f_crypt(int num_arg)
{
    char *salt;
    char *res;
    int i, count = 0;
    
    if (sp->type == T_STRING && strlen(sp->u.string) >= 2)
    {
        /* If the string matches. $<id>$ then we add a salt.
         * If it's $<id>$<salt>$ salt we don't. */
        for (i = 0; i < strlen(sp->u.string); i++)
            if (sp->u.string[i] == '$')
                count++;

        if (count > 1 && count < 3)
        {
            salt = xalloc(strlen(sp->u.string) + MAX_SALT_LENGTH + 2);
            strcpy(salt, sp->u.string);
            res = build_salt(MAX_SALT_LENGTH);
            strcat(salt, res);
            strcat(salt, "$");
            free(res);
        } else {
            salt = xalloc(strlen(sp->u.string) + 1);
            strcpy(salt, sp->u.string);
        }
    } else {
        salt = build_salt(2);
    }
    
    res = make_mstring((char *)crypt((sp-1)->u.string, salt));
    free(salt);
    
    pop_n_elems(2);
    push_mstring(res);
}

/* ARGSUSED */
static void
f_destruct(int num_arg)
{
    destruct_object(current_object);
    push_number(0);
}

static void
f_random(int num_arg)
{
    long long seed;
    struct svalue *arg = sp - num_arg + 1;

    if (num_arg > 1)
    {
	seed = arg[1].u.number;
	pop_stack();
	sp->u.number = random_number(arg[0].u.number, sizeof(seed), (char *)&seed);
    }
    else
	sp->u.number = random_number(arg[0].u.number, 0, NULL);
}

/* ARGSUSED */
static void
f_while(int num_arg)
{
    fatal("F_WHILE should not appear.\n");
}

/* ARGSUSED */
static void
f_do(int num_arg)
{
    fatal("F_DO should not appear.\n");
}

/* ARGSUSED */
static void
f_for(int num_arg)
{
    fatal("F_FOR should not appear.\n");
}

/* ARGSUSED */
static void
f_foreach_m(int num_arg)
{
    struct svalue *key_var = sp - 4, *val_var = sp - 3;
    struct svalue *map = sp - 2, *array = sp - 1, *ix = sp;

    if (map->type == T_NUMBER) {
        pc = current_prog->program + read_short(pc);
        return;
    }
    if (map->type != T_MAPPING)
        bad_arg(3, F_FOREACH_M, map);
    if (ix->u.number >= array->u.vec->size) {
        pc = current_prog->program + read_short(pc);
        return;
    }
    pc += 2;
    assign_svalue(key_var->u.lvalue, array->u.vec->item + ix->u.number);
    assign_svalue(val_var->u.lvalue, get_map_lvalue(map->u.map, key_var->u.lvalue, 0));
    ix->u.number++;
}

/* ARGSUSED */
static void
f_foreach(int num_arg)
{
    struct svalue *loopvar = sp - 2, *array = sp - 1, *ix = sp;

    if (array->type == T_NUMBER) {
        pc = current_prog->program + read_short(pc);
        return;
    }
    if (array->type != T_POINTER)
        bad_arg(2, F_FOREACH, array);
    if (ix->u.number >= array->u.vec->size) {
        pc = current_prog->program + read_short(pc);
        return;
    }
    pc += 2;
    assign_svalue(loopvar->u.lvalue, array->u.vec->item + ix->u.number);
    ix->u.number++;
 
}

/******** function stuff *********/
#define ISEMPTY(ob) (ob.type == T_FUNCTION && ob.u.func->funtype == FUN_EMPTY)

static void
papply(int num_arg)	/* num_arg does not include the function */
{
    int nargs, i, j;
    struct closure *f, *oldf;
    struct vector *items;
    struct svalue *args = sp - num_arg + 1;
    int newsize;

    if ((sp-num_arg)->type != T_FUNCTION)
      bad_arg(1, F_PAPPLY, sp - num_arg);

    oldf = (sp-num_arg)->u.func;
#ifdef FUNCDEBUG
    if (oldf->magic != FUNMAGIC || oldf->ref == 0 || oldf->ref > 100)
	fatal("Bad function argument to papply\n");
#endif
    f = alloc_closure(oldf->funtype);
#ifdef DEBUG
#if 0
    (void)fprintf(stderr, "papply=%lx %s\n", f, show_closure(oldf));
#endif
#endif
    f->funobj = oldf->funobj;
    f->funno = oldf->funno;
    f->funinh = oldf->funinh;
    if (f->funobj)
        add_ref(f->funobj, "f_papply");

    nargs = oldf->funargs->size;
    /* compute the size needed for the new vector */
    for(i = j = 0; i < nargs; i++) {
	if (ISEMPTY(oldf->funargs->item[i]))
	    j++;		/* need an argument to fill the slot */
    }
    newsize = nargs;
    if (j < num_arg)
	newsize += num_arg-j;

    free_vector(f->funargs);
    f->funargs = items = allocate_array(newsize);

    /* march down and fill the slots */
    for(j = i = 0; i < nargs; i++) {
	if (ISEMPTY(oldf->funargs->item[i]) && j < num_arg) {
	    assign_svalue_no_free(&items->item[i], &args[j++]);
	} else {
	    assign_svalue_no_free(&items->item[i], &oldf->funargs->item[i]);
	}
    }
    /* now do the remaining slots */
    for(; j < num_arg; j++)
	assign_svalue(&items->item[i++], &args[j]);
    if (i != newsize)
	fatal("papply\n");
    
    pop_n_elems(num_arg+1);	/* remove function and args */
    push_function(f, FALSE);
}

/* ARGSUSED */
static void
f_papply(int xxx)
{
    int num_arg;

    num_arg = EXTRACT_UCHAR(pc);
    pc++;
    papply(num_arg);
}

/* ARGSUSED */
static void
f_papplyv(int xxx)
{
    int num_arg = 2;
    struct svalue *arg = sp - num_arg + 1;
    struct vector *v;
    int i, size;

    if (arg[0].type != T_FUNCTION)
	fatal("Non-function in f_papply_array\n");
    if (arg[1].type != T_POINTER)
	fatal("Non-array in f_papply_array\n");
    v = arg[1].u.vec;
    INCREF(v->ref);		/* we pop it below */
    pop_n_elems(num_arg-1);	/* remove all except function */
    size = v->size;
    for(i = 0; i < size; i++)
	push_svalue(&v->item[i]);
    free_vector(v);		/* and free it now */
    papply(size);
}

char *
getclosurename(struct closure *f)
{
    if (f->funtype == FUN_EFUN)
	return get_f_name(f->funno);
    else if (f->funtype == FUN_COMPOSE)
	return "@";
    else if (f->funtype == FUN_EMPTY)
	return "_";
    else {
	struct object *ob = f->funobj;

	if (ob->flags & O_DESTRUCTED)
	    return "<<DESTRUCTED>>";
	return ob->prog->inherit[f->funinh].prog->functions[f->funno].name;
    }
}

static void call_efun(int, int);

static char *
write_closure(struct closure *f, char *buf, const char *end)
{
    char *name;
    int i;

    if (f->funtype == FUN_EMPTY)
    {
	if (buf < end)
	    *buf++ = '_';
	*buf = '\0';
	return buf;
    }
    name = getclosurename(f);
    if (f->funtype == FUN_COMPOSE)
    {
	buf = write_closure(f->funargs->item[1].u.func, buf, end);
	if (buf < end)
	     *buf++ = ' ';
	if (buf < end)
	     *buf++ = '@';
	if (buf < end)
	     *buf++ = ' ';
	buf = write_closure(f->funargs->item[0].u.func, buf, end);
	*buf = '\0';
	return buf;
    }
    if (f->funargs && f->funargs->size && buf < end)
	*buf++ = '&';
    if (f->funobj)
    {
	if (buf + 3 < end)
	{
	    (void)sprintf(buf, "\"/%.*s\"", (int)(end - buf), f->funobj->name);
	    buf += strlen(buf);
	}
	if (buf < end)
	    *buf++ = '-';
	if (buf < end)
	    *buf++ = '>';
	if (buf < end)
	{
	    (void)sprintf(buf, "%.*s", (int)(end - buf), name);
	    buf += strlen(buf);
	}
    }
    else if (buf < end)
    {
	(void)sprintf(buf, "%.*s", (int)(end - buf), name);
	buf += strlen(buf);
    }
    if (f->funargs && f->funargs->size)
    {
	if (buf < end)
	    *buf++ = '(';
	for (i = 0; i < f->funargs->size; i++)
	{
	    if (ISEMPTY(f->funargs->item[i]) && buf < end)
		*buf++ = '_';
	    else if (buf < end)
		*buf++ = '?';
	    if (i < f->funargs->size - 1 && buf < end)
		*buf++ = ',';
	    if (buf >= end)
		break;
	}
	if (buf < end)
	    *buf++ = ')';
    }
    *buf = '\0';
    return buf;
}

char *
show_closure(struct closure *f)
{
    static char buf[1024];

    (void)write_closure(f, buf, buf + sizeof(buf) - 1);
    return buf;
}

int
call_var(int num_arg, struct closure *f)
{
    int i, narg;
    int efun;
    struct object *ob;

    struct svalue *insp = sp - num_arg;

    if (!f)
	fatal("NULL closure in call_var\n");

#ifdef FUNCDEBUG
    if (f->magic != FUNMAGIC || f->ref == 0 || f->ref > 100)
	fatal("Bad closure in call_var\n");
#endif
    efun = -1;
    ob = 0;
    switch(f->funtype)
    {
    case FUN_LFUNO:
    case FUN_LFUN: 
    case FUN_SFUN:
	ob = f->funobj;
	break;
    case FUN_EFUN: 
	efun = f->funno;
	break;
    case FUN_EMPTY:
	error("Calling EMPTY closure\n");
	break;
    case FUN_COMPOSE:
	return call_var(num_arg, f->funargs->item[0].u.func) &&
	       call_var(1, f->funargs->item[1].u.func);
    default:
	fatal("bad type in call_var\n");
	break;
    }
#ifdef DEBUG
#if 0
    (void)fprintf(stderr, "local call of %lx %s\n", f, show_closure(f));
#endif
#endif
    if (!ob && efun == -1) {
	error("ob=0 in call_var\n");
    }

    if (ob && (ob->flags & O_DESTRUCTED)) {
	error("Call to function in destructed object\n");
    }
    narg = f->funargs->size;
    if (narg) {
	struct svalue *arg;
	int emptyargs;

	/* count the number of empty args */
	for(emptyargs = i = 0; i < narg; i++)
	    if (ISEMPTY(f->funargs->item[i]))
		emptyargs++;

	if (emptyargs > num_arg) {
	    (void)fprintf(stderr, "bad call empty=%d, num=%d, f=%s\n",
			  emptyargs, num_arg, show_closure(f));
	    error("Too few arguments supplied in function call\n");
	}

	probe_stack(narg);

	/* Need to shuffle the stack, move up all the arguments narg slots */
	/* to be safe. */
	for(i = 0; i < num_arg; i++)
	    sp[-i+narg] = sp[-i];
	sp -= num_arg;		/* move sp temporarily */
	arg = (sp + 1) + narg;	/* new postion of given args */

	/* now we need to push the funargs, but only in the filled slots */
	for(i = 0; i < narg; i++) {
	    if (ISEMPTY(f->funargs->item[i])) {
		*++sp = *arg++;	/* copy from stack */
	    } else {
		push_svalue(&f->funargs->item[i]);
	    }
	}
	 
	/* The remaining stack arguments now need to be moved into
	   place.  emptyargs have already been moved, go on until
	   num_arg.
	   */
	for(i = emptyargs; i < num_arg; i++)
	    *++sp = *arg++;

	num_arg += narg - emptyargs;
    }
    if (ob) {
	/* speed up call by only doing access_* when really needed */
	if (f->funno == 65535)
	    fatal("bad function index\n");
	call_function(ob, f->funinh, (unsigned int)f->funno, num_arg);
    } else {
	/*(void)fprintf(stderr, "call efun %d %d\n", efun, num_arg);*/
	call_efun(efun, num_arg);
    }

if (sp-1 != insp) 
(void)printf("Bad sp in call_var %d\n", (int)(sp-1-insp));
    return 1;
}

/* ARGSUSED */
static void
f_call_var(int xxx)
{
    int num_arg;
    struct closure *f;
    int r;

    num_arg = EXTRACT_UCHAR(pc);
    pc++;

    if (sp->type != T_FUNCTION)
	error("Bad function argument in function call.\n");
    f = sp->u.func;
    sp--;			/* pop function argument, but don't free it */
    /*(void)printf("call_var in  %d %s\n", num_arg, show_closure(f));*/
    r = call_var(num_arg, f);
    /*(void)printf("call_var out %d %s\n", num_arg, show_closure(f));*/
    free_closure(f);		/* hmm, what if we abort?  f leaks. */
    if (!r)
	error("Call of function failed\n");
}

#ifdef FUNCDEBUG
struct closure *allfuncs;
#endif

struct closure *
alloc_closure(int ftype)
{
    struct closure *f;

    f = (struct closure *)xalloc(sizeof(struct closure));
    f->funtype = ftype;
    f->funobj = 0;
    f->funargs = allocate_array(0);
    f->funno = 65535;
    f->ref = 1;
    num_closures++;
    total_closure_size += sizeof(struct closure);
#ifdef FUNCDEBUG
    f->magic = FUNMAGIC;
    f->from = current_object;
    f->next = allfuncs;
    allfuncs = f;
#endif
    return f;
}

int
legal_closure(struct closure *fun)
{
#ifdef FUNCDEBUG
    if (fun->magic != FUNMAGIC || fun->ref == 0 || fun->ref > 100)
	fatal("Bad function in legal_closure\n");
#endif
#ifdef DEBUG
    switch (fun->funtype)
    {
	case FUN_LFUN:
	case FUN_SFUN:
	case FUN_EFUN:
	case FUN_LFUNO:
	case FUN_COMPOSE:
	    break;
	default:
	    fatal("Bad function type in legal_closure\n");
	    /* NOTREACHED */
    }
#endif
    if (fun->funobj && (fun->funobj->flags & O_DESTRUCTED))
	return 0;
    if (fun->funtype == FUN_COMPOSE)
	return legal_closure(fun->funargs->item[0].u.func) &&
	       legal_closure(fun->funargs->item[1].u.func);
    return 1;
}

#define FUNCCACHE 8

static struct closure *funccache[FUNCCACHE];

#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void
clear_closure_cache()
{
    int i;

    for (i = 0; i < FUNCCACHE; i++)
	if (funccache[i]) {
	    free_closure(funccache[i]);
	    funccache[i] = NULL;
	}
}
#endif

static struct closure *
alloc_objclosure(int ftype, int inh, int no, struct object *ob, char *refstr, int usecache)
{
    struct closure *fun;
    static int next = 0;

    if (ftype == FUN_EMPTY)
        fatal("Empty closure allocated");
    
    if (usecache) {
	/* Avoid repeated closure construction in some cases.
	 * Occurs a lot in add_action.
	 */
	int i;
	for(i = 0; i < FUNCCACHE; i++) {
	    if (funccache[i] &&
		ftype == funccache[i]->funtype &&
		ob    == funccache[i]->funobj &&
		no    == funccache[i]->funno  &&
		inh   == funccache[i]->funinh) {
		fun = funccache[i];
		INCREF(fun->ref);
		return fun;
	    }
	}
    }

    fun = alloc_closure(ftype);
    /*(void)printf("alloc_objclosure=%lx %lx %s\n", fun, ob, refstr);*/
    fun->funobj = ob;
    if (ob)
	add_ref(fun->funobj, refstr);
    fun->funno = no;
    fun->funinh = inh;

    /* Add function to cache */
    if (funccache[next])
    {
    	free_closure(funccache[next]);
    }
    
    funccache[next] = fun;
    INCREF(fun->ref);		/* can be referenced from the cache now */
    next = (next+1) % FUNCCACHE;

    return fun;
}

static struct closure *
alloc_objclosurestr(int ftype, char *funstr, struct object *ob, char *refstr, int usecache)
{
    int funinh, funno;

    /* look up function name in object */
    if (!(ob->flags & O_DESTRUCTED) &&
	search_for_function(funstr, ob->prog)) {
	/* maybe overly restrictive, but don't allow everything... */
	if (   (current_object != ob &&
		(function_type_mod_found & (TYPE_MOD_STATIC | TYPE_MOD_PRIVATE)))
	    || ((function_type_mod_found & TYPE_MOD_PRIVATE) &&
		/* mysterious test taken from old set_alarm */
		inh_offset < function_inherit_found - (int)function_prog_found->num_inherited + 1))
	    return 0;
	funno = function_index_found;
	funinh = function_inherit_found;
    } else {
	return 0;
    }
    if (ftype == FUN_LFUNO && ob == current_object)
	ftype = FUN_LFUN;
    return alloc_objclosure(ftype, funinh, funno, ob, refstr, usecache);
}

/* ARGSUSED */
static void
f_mkfunction(int xxx)
{
    int num_arg = 2;
    struct closure *f;
    struct svalue *arg = sp - num_arg + 1;
    struct object *ob;

#if 1
    if (arg[0].type != T_STRING || arg[1].type != T_OBJECT || num_arg < 2 || arg[1].u.ob == 0)
	fatal("bad argument to mkfunction\n");
#endif
    ob = arg[1].u.ob;
    f = alloc_objclosurestr(FUN_LFUNO, arg[0].u.string, ob, "f_mkfunction", 1);
    if (!f) {
	pop_n_elems(num_arg);
	push_number(0);
	return;
    }
    if (ob == current_object)
	f->funtype = FUN_LFUN;

    pop_n_elems(num_arg);
    push_function(f, FALSE);
}

/* ARGSUSED */
static void
f_build_closure(int num_arg)
{
    struct closure *f;
    int ftype;
    struct object *obj;
    unsigned short funno, fun_inh;
    
    ftype = EXTRACT_UCHAR(pc);
    pc++;
    
    if (ftype == FUN_EMPTY) {
	probe_stack(1);
	*++sp = constempty;
	return;
    }

    switch(ftype) {
      case FUN_LFUN_NOMASK:
        fun_inh = EXTRACT_UCHAR(pc);
	pc++;
	((char *)&funno)[0] = pc[0];
	((char *)&funno)[1] = pc[1];
	pc += 2;
	f = alloc_objclosure(FUN_LFUN, inh_offset  + (fun_inh - (current_prog->num_inherited - 1)), funno, current_object, "f_build_closure", 1);
	pop_stack();
        break;
      case FUN_LFUN:
	pc += 3;

	f = alloc_objclosurestr(ftype, sp->u.string, current_object, "f_build_closure", 1);
	pop_stack();
	break;
    case FUN_SFUN:
	if (sp->type != T_STRING)
	    fatal("Not a string in f_build_sclosure\n");
	if (!simul_efun_ob)
	  error("No simul_efun object");
	f = alloc_objclosurestr(ftype, sp->u.string, simul_efun_ob, "f_build_closure", 1);
	pop_stack();
	break;
    case FUN_EFUN:
	((char *)&funno)[0] = pc[0];
	((char *)&funno)[1] = pc[1];
	pc += 2;
	f = alloc_objclosure(ftype, 0, funno, 0, "f_build_closure", 0); /* could cache */
	break;
    case FUN_LFUNO:
	if (sp->type != T_STRING)
	    fatal("Not a string in f_build_closure\n");
	if (sp[-1].type == T_OBJECT) {
	    obj = sp[-1].u.ob;
	} else if (sp[-1].type == T_STRING) {
	    obj = find_object(sp[-1].u.string);
	    if (obj == 0)
		error("Bad object in &\"ob\"->fun\n");
/* XXX Why? */
add_ref(obj, "build_closure string");
	} else {
	    obj = 0;
	    error("Bad object type in &ob->fun\n");
	}

	f = alloc_objclosurestr(ftype, sp->u.string, obj, "f_build_closure", 0); /* could cache */
	pop_stack();
	pop_stack();
	break;
    case FUN_COMPOSE:
	if ((sp-1)->type != T_FUNCTION)
	    error("Bad argument 1 to '@'\n");
	if (sp->type != T_FUNCTION)
	    error("Bad argument 2 to '@'\n");
	f = alloc_closure(ftype);
	free_vector(f->funargs);
	f->funargs = allocate_array(2);
	assign_svalue_no_free(&f->funargs->item[0], sp);
	assign_svalue_no_free(&f->funargs->item[1], sp-1);
	pop_stack();
	pop_stack();
	break;
    default:
	fatal("Bad type in build_closure\n");
	f = 0;			/* make -Wall quiet */
	break;
    }

    if (f) {
	push_function(f, FALSE);
    } else {
	push_number(0);
    }
}

void
free_closure(struct closure *f)
{
    if (f->funtype == FUN_EMPTY)
        return;
    
    if (!f->ref || --f->ref > 0)
	return;
/*(void)printf("free_closure %lx %s\n", f, show_closure(f));*/
#ifdef FUNCDEBUG
    {
	struct closure **p;
	for(p = &allfuncs; *p && *p != f; p = &(*p)->next)
	    ;
	if (!*p)
	    fatal("Closure pointer NULL\n");
	*p = (*p)->next;
    }
#endif

    
    free_vector(f->funargs);
    if (f->funobj)
	free_object(f->funobj, "free_closure");
    num_closures--;
    total_closure_size -= sizeof(struct closure);
    free((char *)f);
}

#ifdef FUNCDEBUG
void
dumpfuncs()
{
    struct closure *f;

    (void)fprintf(stderr, "\nFunction dump:\n");
    for(f = allfuncs; f; f = f->next) {
	if (f->funobj && (f->funobj->flags & O_DESTRUCTED))
	    (void)fprintf(stderr, "***DEST ");
	(void)fprintf(stderr, "%lx: ref=%d %s", (long)f, f->ref, show_closure(f));
	if (f->from != f->funobj)
	    (void)fprintf(stderr, " from %s", f->from->name);
	(void)fprintf(stderr, "\n");
    }
}
#endif

/*********************************/
static int read_long_long(char *addr)
{
    long long ret;

    memcpy(&ret, addr, sizeof(ret));

    return ret;
}

typedef union {
    long long i;
    char *str;
} searchval_t;
	
static long long
cmp_values(searchval_t val, long long cmp, int as_str)
{
    if (as_str) {
	return strcmp(val.str, current_prog->rodata + cmp);
    }
    else
	return (long long)(val.i - cmp);
}

struct case_entry
{
    int value;
    unsigned short offset;
};

/* ARGSUSED */
static void
f_switch(int num_arg)
{
#define TABLE_OFF       1
#define TABLE_END_OFF   3
#define DEFAULT_OFF     5

#define STR_TABLE 1

#define E_VALUE  0
#define E_OFFSET 8
#define ENTRY_SIZE 10

#define RANGE_OFFSET ((unsigned short)-1)

    int tab_head, tab_tail, tab_mid;
    unsigned int tab_start, tab_end;
    searchval_t search_val;
    short tab_type, is_str;

    tab_type = (*pc) & 0xff;
    is_str = tab_type & STR_TABLE;

    tab_start = read_short(pc + TABLE_OFF);
    tab_end = read_short(pc + TABLE_END_OFF);

    tab_head = 0;
    tab_tail = (tab_end - tab_start) / ENTRY_SIZE - 1;
    
    if (is_str)
    {
	/* This table has 0 as case label as first entry in the table */

	if (sp->type == T_NUMBER && !sp->u.number)
	{
	    pc = current_prog->program + read_short(current_prog->program + tab_start + E_OFFSET);
	    pop_stack();
	    return;
	}
	if (sp->type != T_STRING)
	    bad_arg(1, F_SWITCH, sp);
	search_val.str = sp->u.string;
	tab_head++;
    }
    else if (sp->type == T_NUMBER)
	search_val.i = sp->u.number;
    else
	bad_arg(1, F_SWITCH, sp);

    while (tab_head <= tab_tail)
    {
	tab_mid = (tab_head + tab_tail) / 2;

	if (read_short(current_prog->program + tab_start +
		       tab_mid * ENTRY_SIZE + E_OFFSET) == RANGE_OFFSET ||
	    (tab_mid != tab_head &&
	     read_short(current_prog->program + tab_start +
			tab_mid * ENTRY_SIZE + E_OFFSET - ENTRY_SIZE) == RANGE_OFFSET &&
	     (tab_mid--, 1)))
	{
	    /* It is a range entry */
	    long long lo_value, hi_value;

	    lo_value = read_long_long(current_prog->program + tab_start +
				tab_mid * ENTRY_SIZE + E_VALUE);
	    hi_value = read_long_long(current_prog->program + tab_start +
				tab_mid * ENTRY_SIZE + E_VALUE + ENTRY_SIZE);
	    if (cmp_values(search_val, lo_value, is_str) < 0)
		tab_tail = tab_mid - 1;
	    else if (cmp_values(search_val, hi_value, is_str) > 0)
		tab_head = tab_mid + 2;
	    else
	    {
		pc = current_prog->program +
		    read_short(current_prog->program + tab_start +
			       tab_mid * ENTRY_SIZE + E_OFFSET + ENTRY_SIZE);
		pop_stack();
		return;
	    }
	}
	else 
	{
	    /* It is an ordinary entry */
	    long value;
	    int cmp;

	    value = read_long_long(current_prog->program + tab_start +
			     tab_mid * ENTRY_SIZE + E_VALUE);
	    if ((cmp = cmp_values(search_val, value, is_str)) == 0)
	    {
		pc = current_prog->program +
		    read_short(current_prog->program + tab_start +
			       tab_mid * ENTRY_SIZE + E_OFFSET);
		pop_stack();
		return;
	    }
	    else if (cmp < 0)
		tab_tail = tab_mid - 1;
	    else
		tab_head = tab_mid + 1;
	}
    }
    /* No match, use default */
    pc = current_prog->program + read_short(pc + DEFAULT_OFF);
    pop_stack();
    return;
}

/* ARGSUSED */
static void
f_break(int xxx)
{
    error("Bad break code, this should not happend.\n");
}

/* ARGSUSED */
static void
f_subscript(int xxx)
{
    fatal("F_SUBSCRIPT should not appear.\n");
}

/* ARGSUSED */
static void
f_strlen(int xxx)
{
    int i;

    if (sp->type == T_NUMBER)
	i = 0;
    else
	i = strlen(sp->u.string);
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_mkmapping(int num_arg)
{
    struct mapping *mm = 0;

    if ((sp-1)->type == T_POINTER && sp->type == T_POINTER)
	mm = make_mapping((sp-1)->u.vec, sp->u.vec);
    else if ((sp-1)->type == T_NUMBER && sp->type == T_POINTER)
	mm = make_mapping(NULL, sp->u.vec);
    else if ((sp-1)->type == T_POINTER && sp->type == T_NUMBER)
	mm = make_mapping((sp-1)->u.vec, NULL);
    else if ((sp-1)->type == T_NUMBER && sp->type == T_NUMBER)
    {
      bad_arg_op(F_MKMAPPING, sp - 1, sp);
      return;
    }
    pop_n_elems(2);
    push_mapping(mm, FALSE);
}

/* ARGSUSED */
static void
f_m_sizeof(int num_arg)
{
    int i;

    if (sp->type == T_MAPPING)
	i = card_mapping(sp->u.map);
    else
	i = 0;
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_m_indexes(int num_arg)
{
    struct vector *v;

    if (sp->type == T_MAPPING)
    {
	v = map_domain(sp->u.map);
	pop_stack();
	push_vector(v, FALSE);
    }
    else
    {
	pop_stack();
	push_number(0);
    }
}

/* ARGSUSED */
static void
f_m_values(int num_arg)
{
    struct vector *v;

    if (sp->type == T_MAPPING)
    {
	v = map_codomain(sp->u.map);
	pop_stack();
	push_vector(v, FALSE);
    }
    else
    {
	pop_stack();
	push_number(0);
    }
}

/* ARGSUSED */
static void
f_m_delete(int num_arg)
{
    struct mapping *m;

    if ((sp-1)->type == T_MAPPING)
    {
	m = remove_mapping((sp-1)->u.map, sp);
	pop_n_elems(2);
	push_mapping(m, FALSE);
    }
    else
    {
	pop_n_elems(2);
	push_number(0);
    }
}

/* ARGSUSED */
static void
f_sizeof(int num_arg)
{
    int i;

    if (sp->type == T_NUMBER)
	i = 0;
    else
	i = sp->u.vec->size;
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_upper_case(int num_arg)
{
    char *str;
    int  i;

    if (sp->type == T_NUMBER)
	return;
    str = make_mstring(sp->u.string);
    for (i = strlen(str)-1; i>=0; i--)
	str[i] = toupper(str[i]);
    pop_stack();
    push_mstring(str);
}

/* ARGSUSED */
static void
f_lower_case(int num_arg)
{
    char *str;
    int  i;

    if (sp->type == T_NUMBER)
	return;
    str = make_mstring(sp->u.string);
    for (i = strlen(str)-1; i>=0; i--)
	str[i] = tolower(str[i]);
    pop_stack();
    push_mstring(str);
}

#define ISPRINT(c) (isprint(c) || (c) >= 0xa0)

/* ARGSUSED */
static void
f_readable_string(int num_arg)
{
    char *str;
    int  i, c;

    if (sp->type == T_NUMBER)
	return;
    str = make_mstring(sp->u.string);
    for (i = strlen(str)-1; i>=0; i--) {
	c = str[i] & 0xff;
	if (c < ' ' || !ISPRINT(c) ) /* A quick hack for 8859 -- LA */
	    str[i] = '.';
    }
    pop_stack();
    push_mstring(str);
}

/* ARGSUSED */
static void
f_capitalize(int num_arg)
{
    struct svalue *arg = sp - num_arg + 1;

    if (arg[0].type == T_NUMBER)
	return;

    if (islower(arg[0].u.string[0])) {
	char *str;
	str = make_mstring(arg[0].u.string);
	str[0] = toupper(str[0]);
	pop_stack();
	push_mstring(str);
    }
}

/* ARGSUSED */
static void
f_process_string(int num_arg)
{
    extern char *process_string (char *, int);
    char *str;

    str = process_string(sp[-1].u.string, sp->u.number != 0);
    pop_stack();
    if (str)
    {
	pop_stack();
	push_mstring(str);
    }
}

/* ARGSUSED */
static void
f_process_value(int num_arg)
{
    extern struct svalue *process_value (char *, int);
    struct svalue *ret;

    ret = process_value(sp[-1].u.string, sp->u.number != 0);
    pop_stack();
    pop_stack();
    if (ret)
    {
	push_svalue(ret);
    }
    else
	push_number(0);
}

/* ARGSUSED */
static void
f_command(int num_arg)
{
    int i;

    i = command_for_object(sp->u.string, 0);
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_get_dir(int num_arg)
{
    struct vector *v = get_dir(sp->u.string);

    pop_stack();
    if (v) {
	push_vector(v, FALSE);
    } else
	push_number(0);
}

/* ARGSUSED */
static void
f_rm(int num_arg)
{
    int i;

    i = remove_file(sp->u.string);
    pop_stack();
    push_number(i);
}

/* ARGSUSED */
static void
f_mkdir(int num_arg)
{
    char *path;

    path = check_valid_path(sp->u.string, current_object, "mkdir", 1);
    /* pop_stack(); see comment above... */
    if (path == 0 || mkdir(path, 0774) == -1)
	assign_svalue(sp, &const0);
    else
	assign_svalue(sp, &const1);
}

/* ARGSUSED */
static void
f_rmdir(int num_arg)
{
    char *path;

    path = check_valid_path(sp->u.string, current_object, "rmdir", 1);
    /* pop_stack(); rw - what the heck ??? */
    if (path == 0 || rmdir(path) == -1)
	assign_svalue(sp, &const0);
    else
	assign_svalue(sp, &const1);
}

static void
f_input_to(int num_arg)
{
    struct svalue *arg = sp - num_arg + 1;
    struct vector *v;
    int r;
    int flag = 1;

    if (arg[0].type == T_FUNCTION) {
	if (num_arg > 2)
	    error("Bad # of args to input_to()\n");
    } else if (arg[0].type == T_STRING) {
	struct closure *fun;
	int i, size;

	fun = alloc_objclosurestr(FUN_LFUNO, arg[0].u.string, current_object, "f_input_to", 0);
	if (!fun) {
	    pop_n_elems(num_arg);
	    push_number(0);
	    return;
	}

	size = num_arg > 2 ? num_arg - 2 : 0;
	if (size > 0) {
	    v = allocate_array(size+1);
	    v->item[0] = constempty; /* slot for the input_to string */
	    for (i = 2; i < num_arg; i++)
		assign_svalue_no_free(&v->item[i - 1], &arg[i]);
	    free_vector(fun->funargs);
	    fun->funargs = v;
	}

	free_svalue(&arg[0]);	/* release old stack location */
	arg[0].type = T_FUNCTION;
	arg[0].u.func = fun;	/* and put in a new one */

	WARNOBSOLETE(current_object, "string as function in input_to");
    } else
      bad_arg(1, F_INPUT_TO, arg);

    if (num_arg == 1 || (arg[1].type == T_NUMBER && arg[1].u.number == 0))
	flag = 0;
    r = input_to(arg[0].u.func, flag);
    pop_n_elems(num_arg);
    push_number(r);
}

/* ARGSUSED */
static void
f_set_living_name(int num_arg)
{
    set_living_name(current_object, sp->u.string);
}

/* ARGSUSED */
static void
f_query_living_name(int num_arg)
{
    char *str;

    if (sp->type == T_NUMBER)
    {
       assign_svalue(sp, &const0);
       return;
    }

    str = sp->u.ob->living_name;
    pop_stack();

    if (str)
       push_string(str, STRING_SSTRING);
    else
       push_number(0);
}


static void
f_parse_command(int num_arg)
{
    struct svalue *arg;
    int	i;

    num_arg = EXTRACT_UCHAR(pc);
    pc++;
    arg = sp - num_arg + 1;
    if (arg[0].type != T_STRING)
	bad_arg(1, F_PARSE_COMMAND, &arg[0]);
    if (arg[1].type != T_OBJECT && arg[1].type != T_POINTER)
	bad_arg(2, F_PARSE_COMMAND, &arg[1]);
    if (arg[2].type != T_STRING)
	bad_arg(3, F_PARSE_COMMAND, &arg[2]);
    if (arg[1].type == T_POINTER)
	check_for_destr(&arg[1]);

    i = parse(arg[0].u.string, &arg[1], arg[2].u.string, &arg[3],
	      num_arg-3); 
    pop_n_elems(num_arg);	/* Get rid of all arguments */
    push_number(i);		/* Push the result value */
}

static void
f_debug(int num_arg)
{
    struct svalue *arg, *ret;
    int i;

    arg = sp - num_arg + 1;
    if (current_object != master_ob)
    {
	push_object(current_object);
	for (i = 0; i < num_arg; i++)
	    push_svalue(&arg[i]);
	ret = apply_master_ob(M_VALID_DEBUG, num_arg + 1);
	if (ret && (ret->type != T_NUMBER || ret->u.number == 0))
	{
	    pop_n_elems(num_arg);	/* Get rid of all arguments */
	    push_number(0);
	    return;
	}
    }
    arg = sp - num_arg + 1;
    if (arg[0].type != T_STRING)
	bad_arg(1, F_DEBUG, &arg[0]);

    ret = debug_command(arg[0].u.string,
			num_arg-1, sp-num_arg+2);
    pop_n_elems(num_arg - 1);	/* Get rid of all arguments */
    assign_svalue(sp, ret);
    free_svalue(ret);
}

/* ARGSUSED */
static void
f_sscanf(int xxx)
{
    int i;
    int num_arg;

    num_arg = EXTRACT_UCHAR(pc);
    pc++;
    i = inter_sscanf(num_arg);
    pop_n_elems(num_arg);
    push_number(i);
}

/* ARGSUSED */
static void
f_enable_commands(int num_arg)
{
    enable_commands(1);
    push_number(1);
}

/* ARGSUSED */
static void
f_disable_commands(int num_arg)
{
    enable_commands(0);
    push_number(0);
}

/* ARGSUSED */
static void
f_present(int num_arg)
{
    struct object *ob;

    if ((sp-1)->type == T_NUMBER) {
	if ((sp-1)->u.number != 0)
	  bad_arg(1, F_PRESENT, sp - 1);
	ob = 0;
    } else
	ob = object_present((sp-1), sp);
    pop_stack();
    pop_stack();
    push_object(ob);
}

/* ARGSUSED */
static void
f_const0(int num_arg)
{
    push_number(0);
}

/* ARGSUSED */
static void
f_const1(int num_arg)
{
    push_number(1);
}

/* ARGSUSED */
static void
f_number(int num_arg)
{
    long long i;

    memcpy(&i, pc, sizeof(i));
    pc += sizeof(i);
    push_number(i);
}

/* ARGSUSED */
static void
f_assign(int num_arg)
{
#ifdef DEBUG
    if (sp[-1].type != T_LVALUE)
	fatal("Bad argument to F_ASSIGN\n");
#endif
    assign_svalue((sp-1)->u.lvalue, sp);
    sp--;
    *sp = *(sp+1);
}

/* ARGSUSED */
static void
f_ctime(int num_arg)
{
    char *cp1, *cp2;

    cp1 = time_string(sp->u.number);
    cp2 = strchr(cp1, '\n');
    if (cp2)
	*cp2 = '\0';
    cp1 = make_mstring(cp1);
    pop_stack();
    push_mstring(cp1);
}

/* ARGSUSED */
static void
f_add_eq(int num_arg)
{
    struct svalue *argp;
    char *new_str;
    long long  i;
    double fl;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to +=");
    argp = sp[-1].u.lvalue;
    switch(argp->type) 
    {
	case T_STRING:
	    if (sp->type == T_STRING) 
	    {
		int l = strlen(argp->u.string);
		new_str = allocate_mstring(l + strlen(sp->u.string));
		(void)strcpy(new_str, argp->u.string);
		(void)strcpy(new_str+l, sp->u.string);
		pop_n_elems(2);
		push_mstring(new_str);
	    } 
	    else if (sp->type == T_NUMBER) 
	    {
		char buff[20];
		(void)sprintf(buff, "%lld", sp->u.number);
		new_str = allocate_mstring(strlen(argp->u.string) + strlen(buff));
		(void)strcpy(new_str, argp->u.string);
		(void)strcat(new_str, buff);
		pop_n_elems(2);
		push_mstring(new_str);
	    } 
	    else 
	    {
		bad_arg_op(F_ADD_EQ, argp, sp);
	    }
	    break;
	case T_NUMBER:
	    if (sp->type == T_NUMBER) 
	    {
		i = argp->u.number + sp->u.number;
		pop_n_elems(2);
		push_number(i);
	    } 
	    else 
	    {
		bad_arg_op(F_ADD_EQ, argp, sp);
	    }
	    break;
	case T_FLOAT:
	    if (sp->type == T_FLOAT) 
	    {
		fl = argp->u.real + sp->u.real;
		pop_n_elems(2);
		push_float(fl);
	    } 
	    else 
	    {
		bad_arg_op(F_ADD_EQ, argp, sp);
	    }
	    break;
	case T_MAPPING:
	    if (sp->type != T_MAPPING) {
		bad_arg_op(F_ADD_EQ, argp, sp);
	    }
	    else {
		struct mapping *m;

		check_for_destr(argp);
		check_for_destr(sp);
		addto_mapping(argp->u.map, sp->u.map);
		m = argp->u.map;
		INCREF(m->ref);
		pop_n_elems(2);
		push_mapping(m, FALSE);
	    }
	    break;
	case T_POINTER:
	    if (sp->type != T_POINTER) {
		bad_arg_op(F_ADD_EQ, argp, sp);
	    }
	    else {
		struct vector *v;

		check_for_destr(argp);
		check_for_destr(sp);
		v = add_array(argp->u.vec, sp->u.vec);
		pop_n_elems(2);
		push_vector(v, FALSE);
	    }
	    break;	      
	default:
	  bad_arg_op(F_ADD_EQ, argp, sp);
    }
    assign_svalue(argp, sp);
}

/* ARGSUSED */
static void
f_sub_eq(int num_arg)
{
    struct svalue *argp;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to -=");
    argp = sp[-1].u.lvalue;
    switch (argp->type) {
	case T_NUMBER:
	    if (sp->type != T_NUMBER)
		bad_arg_op(F_SUB_EQ, argp, sp);
	    argp->u.number -= sp->u.number;
	    sp--;
	    break;
	case T_FLOAT:
	    if (sp->type != T_FLOAT)
	      bad_arg_op(F_SUB_EQ, argp, sp);
	    FLOATASGOP(argp->u.real, -= , sp->u.real);
	    sp--;
	    break;
	case T_POINTER:
	    {
		struct vector *v;
		
		if (sp->type != T_POINTER)
		  bad_arg_op(F_SUB_EQ, argp, sp);

		check_for_destr(argp);
		check_for_destr(sp);

		v = subtract_array(argp->u.vec,  sp->u.vec);
		
		pop_stack();
		pop_stack();
		
		if (v == 0) 
		{
		    push_number(0);
		} 
		else 
		{
		    push_vector(v, FALSE);
		}
		
		assign_svalue(argp, sp);
		break;
	    }
	default:
	  bad_arg_op(F_SUB_EQ, argp, sp);
    }
    assign_svalue(sp, argp);
}

/* ARGSUSED */
static void
f_mult_eq(int num_arg)
{
    struct svalue *argp;
    double fl;
    long long i;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to *=");
    argp = sp[-1].u.lvalue;
    if (argp->type == T_FLOAT && sp->type == T_FLOAT)
    {
	fl = argp->u.real * sp->u.real;
	pop_n_elems(2);
	push_float(fl);
	assign_svalue(argp, sp);
        return;
    }
    if (argp->type == T_NUMBER) {
	if (sp->type == T_NUMBER) {
	    i = argp->u.number * sp->u.number;
	    pop_n_elems(2);
	    push_number(i);
	    assign_svalue(argp, sp);
	    return;
	}
        else if (sp->type == T_STRING) {
            char *result = multiply_string(sp->u.string, argp->u.number);
	    pop_stack();
	    pop_stack();
	    push_string(result, STRING_MSTRING);
	    assign_svalue(argp, sp);
	    return;
        }
        else if (sp->type == T_POINTER) {
	    struct vector *result = multiply_array(sp->u.vec,
						   argp->u.number);
	    pop_stack();
	    pop_stack();
	    push_vector(result, 0);
	    assign_svalue(argp, sp);
            return;
        }
    } else if (argp->type == T_POINTER &&
	       sp->type == T_NUMBER) {
	    struct vector *result = multiply_array(argp->u.vec,
						   sp->u.number);
	    pop_stack();
	    pop_stack();
	    push_vector(result, 0);
	    assign_svalue(argp, sp);
            return;
   } else if (argp->type == T_STRING &&
	       sp->type == T_NUMBER) {
	char *result = multiply_string(argp->u.string, sp->u.number);
	pop_stack();
	pop_stack();
	push_string(result, STRING_MSTRING);
	assign_svalue(argp, sp);
	return;
    }
    bad_arg_op(F_MULT_EQ, argp, sp);
}

/* ARGSUSED */
static void
f_and_eq(int num_arg)
{
    struct svalue *argp;
    long long i;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to &=");
    argp = sp[-1].u.lvalue;
    switch (argp->type)
    {
	case T_NUMBER:
	    if (sp->type != T_NUMBER)
	      bad_arg_op(F_AND_EQ, argp, sp);
	    i = argp->u.number & sp->u.number;
	    pop_n_elems(2);
	    push_number(i);
	    assign_svalue(argp, sp);
	    break;
	case T_POINTER:
	    {
		struct vector *v;
		
		if (sp->type != T_POINTER)
		  bad_arg_op(F_AND_EQ, argp, sp);
		
		v = intersect_array(argp->u.vec,  sp->u.vec);
		
		pop_stack();
		pop_stack();
		
		if (v == 0) 
		{
		    push_number(0);
		} 
		else 
		{
		    push_vector(v, FALSE);
		}
		
		assign_svalue(argp, sp);
		break;
	    }
	default:
	  bad_arg_op(F_AND_EQ, argp, sp);
    }
}

/* ARGSUSED */
static void
f_or_eq(int num_arg)
{
    struct svalue *argp;
    long long i;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to |=");
    argp = sp[-1].u.lvalue;
    if (sp->type == T_POINTER && argp->type == T_POINTER)
    {
	struct vector *v;

	v = union_array(argp->u.vec, sp->u.vec);

	pop_stack();
	pop_stack();

	if (v == NULL)
	    push_number(0);
	else
	    push_vector(v, FALSE);
	assign_svalue(argp,sp);
	return;
    }
    if (argp->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_OR_EQ, argp, sp);
    i = argp->u.number | sp->u.number;
    pop_n_elems(2);
    push_number(i);
    assign_svalue(argp, sp);
}

/* ARGSUSED */
static void
f_xor_eq(int num_arg)
{
    struct svalue *argp;
    long long i;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to ^=");
    argp = sp[-1].u.lvalue;
    if (argp->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_XOR_EQ, argp, sp);
    i = argp->u.number ^ sp->u.number;
    pop_n_elems(2);
    push_number(i);
    assign_svalue(argp, sp);
}

/* ARGSUSED */
static void
f_lsh_eq(int num_arg)
{
    struct svalue *argp;
    long long i;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to <<=");
    argp = sp[-1].u.lvalue;
    if (argp->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_LSH_EQ, argp, sp);
    i = argp->u.number << sp->u.number;
    pop_n_elems(2);
    push_number(i);
    assign_svalue(argp, sp);
}

/* ARGSUSED */
static void
f_rsh_eq(int num_arg)
{
    struct svalue *argp;
    long long i;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to >>=");
    argp = sp[-1].u.lvalue;
    if (argp->type != T_NUMBER ||
	sp->type != T_NUMBER)
      bad_arg_op(F_RSH_EQ, argp, sp);
    i = (int)((unsigned)argp->u.number >> sp->u.number);
    pop_n_elems(2);
    push_number(i);
    assign_svalue(argp, sp);
}

#ifdef F_COMBINE_FREE_LIST
/* ARGSUSED */
static void
f_combine_free_list(int num_arg)
{
    push_number(0);
}
#endif

/* ARGSUSED */
static void
f_div_eq(int num_arg)
{
    struct svalue *argp;
    long long i;
    double fl;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to /=");
    argp = sp[-1].u.lvalue;
    if (argp->type == T_FLOAT && sp->type == T_FLOAT)
    {
	if (sp->u.real == 0.0)
	    error("Division by 0\n");
        fl = argp->u.real / sp->u.real;
	pop_n_elems(2);
	push_float(fl);
	assign_svalue(argp, sp);
        return;
    }
    if (argp->type != T_NUMBER ||
	sp->type != T_NUMBER)
        bad_arg_op(F_DIV_EQ, argp, sp);
    if (sp->u.number == 0)
	error("Division by 0\n");
    i = argp->u.number / sp->u.number;
    pop_n_elems(2);
    push_number(i);
    assign_svalue(argp, sp);
}

/* ARGSUSED */
static void
f_mod_eq(int num_arg)
{
    struct svalue *argp;

    if (sp[-1].type != T_LVALUE)
      error("Non-lvalue argument to %%=");
    argp = sp[-1].u.lvalue;
    if (argp->type != sp->type ||
        (sp->type != T_NUMBER && sp->type != T_FLOAT))
      bad_arg_op(F_MOD_EQ, argp, sp);

    if (sp->type == T_NUMBER) {
      long long i;
      if (sp->u.number == 0)
	error("Modulus by zero.\n");
      i = argp->u.number = argp->u.number % sp->u.number;
      pop_n_elems(2);
      push_number(i);
    } else if (sp->type == T_FLOAT) {
      double d;
      errno = 0;
      d = argp->u.real = fmod(argp->u.real, sp->u.real);
      if (errno)
	error("Modulus by zero.\n");
      pop_n_elems(2);
      push_float(d);
    } 
}

/* ARGSUSED */
static void
f_string(int num_arg)
{
    unsigned short string_number;

    ((char *)&string_number)[0] = pc[0];
    ((char *)&string_number)[1] = pc[1];
    pc += 2;
    push_string(current_prog->rodata + string_number,
		STRING_SSTRING);
}

static void
f_unique_array(int num_arg)
{
    struct vector *res;
    struct svalue *arg;
    struct closure *fun;

    if ((sp - (num_arg - 1))->type == T_NUMBER)
    {
	pop_n_elems(num_arg);
	push_number(0);
	return;
    }

    arg = sp - num_arg + 1;

    if (arg[1].type == T_STRING)
    {
        /* create a function, &call_other(, "function name") */
	fun = alloc_objclosure(FUN_EFUN, 0, F_CALL_OTHER, 0, "f_unique_array", 0);
        free_vector(fun->funargs);
        fun->funargs = allocate_array(2);

        assign_svalue_no_free(&fun->funargs->item[0], &constempty);
        assign_svalue_no_free(&fun->funargs->item[1], &arg[1]);
    }
    else
    {        
        fun = arg[1].u.func;
    }

    if (num_arg < 3)
    {
	check_for_destr(arg);
	res = make_unique((arg)->u.vec, fun, &const0);
    }
    else
    {
	check_for_destr(arg);
	res = make_unique((arg)->u.vec, fun, sp);
	pop_stack ();
    }

    
    if (arg[1].type == T_STRING)
    {
        free_closure(fun);
    }
    
    pop_n_elems(2);

    if (res)
    {
	push_vector(res, FALSE);
    }
    else
    {
	push_number (0);
    }
}

/* ARGSUSED */
static void
f_rename(int num_arg)
{
    int i;

    i = do_rename((sp-1)->u.string, sp->u.string);
    pop_n_elems(2);
    push_number(i);
}

static void
f_map(int num_arg)
{
    struct vector *res;
    struct svalue *arg;
    struct mapping *m;
    struct object *ob;

    arg = sp - num_arg + 1;

    if (num_arg == 2 && arg[1].type == T_FUNCTION) {
	;
    } else if (num_arg >= 3 && arg[1].type == T_STRING) {
	struct closure *fun;

	if (arg[2].type == T_OBJECT)
	    ob = arg[2].u.ob;
	else if (arg[2].type == T_STRING) 
	    ob = find_object(arg[2].u.string);
	else
	    ob = 0;

	if (!ob)
	    bad_arg(3, F_MAP, &arg[2]);

	/* Fake a function */
	fun = alloc_objclosurestr(FUN_LFUNO, arg[1].u.string, ob, "f_map", 0);
	if (!fun) {
	    /* We have three choices here:
	     * 1 - return ({}) which is backwards compatible
	     * 2 - return 0 to indicate an error
	     * 3 - generate a runtime error
	     */
#if 0
	    error("Function used in map could not be found: %s\n", arg[1].u.string);
#else
	    (void)printf("Function used in map could not be found: %s\n", arg[1].u.string);
	    pop_n_elems(num_arg);
	    push_number(0);
#endif
	    return;
	}
	if (num_arg > 3) {
	    free_vector(fun->funargs);
	    fun->funargs = allocate_array(2);
	    fun->funargs->item[0] = constempty;
	    assign_svalue_no_free(&fun->funargs->item[1], &arg[3]);
	}
	free_svalue(&arg[1]);	/* release old stack location */
	arg[1].type = T_FUNCTION;
	arg[1].u.func = fun;	/* and put in a new one */

	WARNOBSOLETE(current_object, "string as function in map");

    } else
	error("Bad arguments to map\n");

    if (arg[0].type == T_POINTER) {
	check_for_destr(&arg[0]);
	res = map_array(arg[0].u.vec, arg[1].u.func);
	pop_n_elems(num_arg);
	if (res) {
	    push_vector(res, FALSE);
	} else
	    push_number(0);
    } else if (arg[0].type == T_MAPPING) {
	check_for_destr(&arg[0]);
	m = map_map(arg[0].u.map, arg[1].u.func);
	pop_n_elems(num_arg);
	if (m) {
	    push_mapping(m, FALSE);
	} else
	    push_number(0);
    } else {
	/*bad_arg(1, F_MAP, &arg[0]);*/
	pop_n_elems(num_arg);
	push_number(0);
    }
}

/* ARGSUSED */
static void
f_sqrt(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = sqrt(arg);
    if (errno)
        error("Argument %.18g to sqrt() is out of bounds.\n", arg);
}

/* ARGSUSED */
static void
f_match_path(int num_arg)
{
    char *scp, *dcp;
    struct svalue string, *svp1, *svp2;

    scp = sp[0].u.string;
    dcp = xalloc(strlen(scp) + 1);

    string.type = T_STRING;
    string.string_type = STRING_CSTRING;
    string.u.string = dcp;

    svp1 = &const0;

    if (*scp != '\0')
    {
	for (;;)
	{
	    while (*scp != '\0' && *scp != '/')
		*dcp++ = *scp++;
	    while (*scp == '/')
		scp++;
	    if (dcp == string.u.string)
		*dcp++ = '/';
	    *dcp = '\0';
	    svp2 = get_map_lvalue(sp[-1].u.map, &string, 0);
	    if (svp2 != &const0)
		svp1 = svp2;
	    if (*scp == '\0')
		break;
	    if (dcp[-1] != '/')
		*dcp++ = '/';
	}
    }

    free(string.u.string);

    pop_stack();

    assign_svalue(sp, svp1);
}


#define EFUN_TABLE
#include "efun_table.h"
#undef EFUN_TABLE

static void 
eval_instruction(char *p)
{
    int num_arg;
    int i;
    int instruction;
    unsigned short ext_instr;
#ifdef DEBUG
    struct svalue *expected_stack;
#endif
    static int catch_level;

    pc = p;
 again:
    i = instruction = EXTRACT_UCHAR(pc);

    if (instruction == F_EXT - EFUN_FIRST)
    {
	((char *)&ext_instr)[0] = pc[1];
	((char *)&ext_instr)[1] = pc[2];
	instruction = ext_instr;
    }

#ifdef TRACE_CODE
    previous_instruction[last] = instruction + EFUN_FIRST;
    previous_pc[last] = pc - current_prog->program;
    reference_prog(current_prog, "trace code");
    if (previous_prog[last])
	free_prog(previous_prog[last]);
    previous_prog[last] = current_prog;
    stack_size[last] = sp - fp - csp->num_local_variables;
    curtracedepth[last] = tracedepth;
    last = (last + 1) % TRACE_SIZE;
#endif

    if (i == F_EXT - EFUN_FIRST)
	pc += 2;

    pc++;
    eval_cost++;
    if (!unlimited && eval_cost > MAX_COST)
    {
	char *line;
	line = get_srccode_position(pc - current_prog->program, current_prog);
	(void)printf("eval_cost too big %d (%s)\n", eval_cost, line);
        eval_cost = 0;
	error("Too long evaluation. Execution aborted.\n");
    }
    /*
     * Execute current instruction. Note that all functions callable
     * from LPC must return a value. This does not apply to control
     * instructions, like F_JUMP.
     */
    { 
#ifdef DEBUG
	int xnum_arg;
#endif
	if (instrs[instruction].min_arg != instrs[instruction].max_arg)
	{
	    num_arg = EXTRACT_UCHAR(pc);
#ifdef DEBUG
	    xnum_arg = num_arg;
#endif
	    pc++;
	}
	else
	{
#ifdef DEBUG
	    xnum_arg = -1;
#endif
	    num_arg = instrs[instruction].min_arg;
	}
	if (num_arg > 0)
	{
	    int type1 = (sp-num_arg+1)->type, type2 = (sp-num_arg+2)->type;
	    if (instrs[instruction].type[0] != 0 &&
		(instrs[instruction].type[0] & type1) == 0)
	    {
		bad_arg(1, instruction + EFUN_FIRST, sp-num_arg+1);
	    }
	    if (num_arg > 1)
	    {
		if (instrs[instruction].type[1] != 0 &&
		    (instrs[instruction].type[1] & type2) == 0)
		{
		    bad_arg(2, instruction + EFUN_FIRST, sp-num_arg+2);
		}
	    }
	}
	/*
         * Safety measure. It is supposed that the evaluator knows
	 * the number of arguments.
	 */
#ifdef DEBUG
	if (xnum_arg != -1 && instruction + EFUN_FIRST != F_CALL_SELF) {
	    expected_stack = sp - num_arg + 1;
	} else {
	    expected_stack = 0;
	}
#endif
    }
    instruction += EFUN_FIRST;
#ifdef OPCPROF
    if (instruction >= 0 && instruction < MAXOPC)
	opcount[instruction]++;
#endif
    /*
     * Execute the instructions. The number of arguments are correct,
     * and the type of the two first arguments are also correct.
     */
#ifdef TRACE_CODE
    if (TRACEP(TRACE_EXEC)) {
	do_trace("Exec ", get_f_name(instruction), "\n");
    }
#endif
    switch(instruction)
    {
    default:
#ifdef DEBUG
	if (instruction >= EFUN_FIRST && instruction <= EFUN_LAST)
#endif
	    efun_table[instruction - EFUN_FIRST](num_arg);
#ifdef DEBUG
	else
	    fatal("Undefined instruction %s (%d)\n", get_f_name(instruction),
		  instruction);
#endif
	break;



    case F_RETURN:
	{
	    int do_return = csp->extern_call;
	    
	    if (sp > fp) {
		struct svalue *sv;

		sv = sp--;
		/*
		 * Deallocate frame and return.
		 */
                while (sp >= fp)
		    pop_stack();
		
		*++sp = *sv;	/* This way, the same ref counts are maintained */
	    }
#ifdef DEBUG
	    if (sp != fp) {
		fatal("Bad stack at F_RETURN\n"); /* marion */
	    }
	    
#endif
	    
	    pop_control_stack();
	    tracedepth--;
#ifdef TRACE_CODE
	    if (TRACEP(TRACE_RETURN)) {
		do_trace("Return", "", "");
		if (TRACETST(TRACE_ARGS)) {
		    write_socket(string_print_formatted(0, " with value: %O", 1, sp),
				 command_giver);
		}
		write_socket("\n", command_giver);
	    }
#endif

	    if (do_return) {	/* The control stack was popped just before */
		return;
	    }
	    
	}
	
	break;

    case F_TRY:
        {
	    struct gdexception exception_frame;
	    char *new_pc;
#ifdef DEBUG
	    struct svalue *stack;
	    int args, ins;
#endif
	    push_pop_error_context (1);
	    catch_level++;

	    pc += 3;
	    
#ifdef DEBUG
	    stack = expected_stack;
	    ins = instruction;
	    args = num_arg;
#endif
	    /* signal catch OK - print no err msg */
	    if (setjmp(exception_frame.e_context))
	    {
		/*
		 * They did a throw() or error. That means that the control
		 * stack must be restored manually here.
		 * Restore the value of expected_stack also. It is always 0
		 * for catch().
		 */
		unsigned int varnum;
#ifdef DEBUG
		num_arg = args;
		instruction = ins;
		expected_stack = 0;
#endif		      
		push_pop_error_context (-1);
		catch_level--;
		new_pc = current_prog->program + read_short(pc);
		pc += 2;
		varnum = EXTRACT_UCHAR(pc);
		assign_svalue(fp + varnum, &catch_value);
		pc = new_pc;
		/* If it was eval_cost too big when cant really catch it */
		if (eval_cost == 0) {
		    eval_cost = MAX_COST;
		    if (catch_level == 0)
			eval_cost -= EXTRA_COST;
		}
	    } else {
#ifdef DEBUG
		num_arg = args;
		instruction = ins;
		expected_stack = stack;
#endif
		exception_frame.e_exception = exception;
		exception_frame.e_catch = 1;
		exception = &exception_frame;

		eval_instruction(pc);
	    }
	    
	    /* next error will return 1 by default */
	    assign_svalue(&catch_value, &const1);
	    break;
	case F_END_TRY:
	    new_pc = pc;
	    push_pop_error_context(-1);
	    catch_level--;
	    pc = new_pc;
	    return;
	}
    case F_CATCH:
	/*
	 * Catch/Throw - catch errors in system or other peoples routines.
	 */
	{
	    struct gdexception exception_frame;
	    unsigned short new_pc_offset;
#ifdef DEBUG
	    struct svalue *stack;
	    int args, ins;
#endif
	    char *old_pc;

	    /*
	     * Compute address of next instruction after the CATCH statement.
	     */
	    ((char *)&new_pc_offset)[0] = pc[0];
	    ((char *)&new_pc_offset)[1] = pc[1];
	    pc += 2;
	    /*
	     * Save some global variables that must be restored separately
	     * after a longjmp. The stack will have to be manually popped all
	     * the way.
	     */
	    old_pc = pc;
	    pc = current_prog->program + new_pc_offset; /* save with pc == where to continue */
	    push_pop_error_context (1);
	    catch_level++;
	    pc = old_pc;
	    /*
	     * We save and restore expected_stack, instruction and num_arg
	     * here to work around problems with some implementations of
	     * setjmp/longjmp
	     */
#ifdef DEBUG
	    stack = expected_stack;
	    ins = instruction;
	    args = num_arg;
#endif
	    /* signal catch OK - print no err msg */
	    if (setjmp(exception_frame.e_context))
	    {
		/*
		 * They did a throw() or error. That means that the control
		 * stack must be restored manually here.
		 * Restore the value of expected_stack also. It is always 0
		 * for catch().
		 */
#ifdef DEBUG
		num_arg = args;
		instruction = ins;
		expected_stack = 0;
#endif
		push_pop_error_context (-1);
		catch_level--;
		push_svalue(&catch_value);
		
		/* If it was eval_cost too big when cant really catch it */
		if (eval_cost == 0) {
		    eval_cost = MAX_COST;
		    if (catch_level == 0)
			eval_cost -= EXTRA_COST;
		}
	    } else {
#ifdef DEBUG
		num_arg = args;
		instruction = ins;
		expected_stack = stack;
#endif
		exception_frame.e_exception = exception;
		exception_frame.e_catch = 1;
		exception = &exception_frame;

		eval_instruction(pc);
	    }
	    
	    /* next error will return 1 by default */
	    assign_svalue(&catch_value, &const1);
	    break;
	case F_END_CATCH:
	    push_pop_error_context(-1);
	    catch_level--;
	    push_svalue(&const0);
	    return;
	}
    }
#ifdef DEBUG
    if ((expected_stack && expected_stack != sp) ||
	sp < fp + csp->num_local_variables - 1)
    {
	fatal("Bad stack after evaluation. Was %d, expected %d, frame ends at %d. Instruction %d, num arg %d\n",
	      sp - start_of_stack,
	      expected_stack - start_of_stack,
	      (fp - start_of_stack) + csp->num_local_variables - 1,
	    instruction, num_arg);
    }
#endif /* DEBUG */
    goto again;
}
	    
#ifdef GLOBAL_CACHE
struct fcache1 {
    int		 	tp;
    char 		*fn;
    int			ff_inh;
    int			ff_ix;
};
#endif


int
s_f_f(char *name, struct program *prog)
{
    int probe = 0, i;
    struct program *cprog = prog;
    int type_mod;
#ifdef GLOBAL_CACHE
    static struct fcache1 fc[GLOBAL_CACHE];
    int global_hash_val;
    extern unsigned long long globcache_hits;
    extern unsigned long long globcache_tries;
#endif
    
    if (!name)
	return 0;
    
#ifdef GLOBAL_CACHE
	    
    /* 
     * Are we looking for the same function in the same program again?
     * This is common for map, filter etc
     */
    globcache_tries++;

    global_hash_val = (int)(((unsigned long)prog / sizeof(void *)) ^
			    ((unsigned long)prog >> 16) ^
			    ((unsigned long)name / sizeof(void *)) ^
			    ((unsigned long)name >> 16)) & (GLOBAL_CACHE - 1);
    
    if (fc[global_hash_val].tp == prog->id_number &&
	fc[global_hash_val].fn == name)
    {
	globcache_hits++;
#ifdef CACHE_STATS
	global_first_saves += prog->num_inherited - fc[global_hash_val].ff_inh;
#endif
	function_inherit_found = fc[global_hash_val].ff_inh;
	function_index_found = fc[global_hash_val].ff_ix;
	if (function_inherit_found != -1)
	{
	    int type_mod1 = prog->inherit[function_inherit_found].type;
	    
	    function_prog_found = prog->inherit[function_inherit_found].prog;
	    function_type_mod_found = function_prog_found->
		functions[function_index_found].type_flags & TYPE_MOD_MASK ;

	    /* Correct function_type_mod_found */
	    if (function_type_mod_found & TYPE_MOD_PRIVATE)
		type_mod1 &= ~TYPE_MOD_PUBLIC;
	    if (function_type_mod_found & TYPE_MOD_PUBLIC)
		type_mod1 &= ~TYPE_MOD_PRIVATE;
	    function_type_mod_found |= type_mod1;
	    return 1;
	}
	else
	{
	    function_prog_found = 0;
	    return 0;
	}
	
    }
    fc[global_hash_val].tp = prog->id_number;
    fc[global_hash_val].fn = name;
    fc[global_hash_val].ff_inh = -1;
#endif

#ifdef CACHE_STATS
    searches_needed += prog->num_inherited;
#endif
    i = prog->num_inherited - 1;
    cprog = prog;
    for (;;)
    {
	
	/* Beware of empty function lists */
#ifdef CACHE_STATS
	    searches_done++;
#endif
	if (cprog->num_functions)
	{
	    /* hash 
	     */
	    probe = PTR_HASH(name, cprog->num_functions);
	    /* Select the right one from the chain 
	     */
	    while (name != cprog->func_hash[probe].name && probe >= 0)
		probe = cprog->func_hash[probe].next_hashed_function;
	    
	    if (probe >= 0)
	    {
		probe = cprog->func_hash[probe].func_index;
		break;
	    }
	}
	if (--i < 0)
	    return 0;
	
	cprog = prog->inherit[i].prog;

    }

    /* Found. Undefined prototypes cannot occur in compiled programs 
	*/
#ifdef CACHE_STATS
    searches_needed -= i;
#endif

#ifdef GLOBAL_CACHE
    fc[global_hash_val].ff_inh = 
#endif
	function_inherit_found = i;
    
    function_prog_found = prog->inherit[i].prog;
#ifdef GLOBAL_CACHE
    fc[global_hash_val].ff_ix =
#endif
	function_index_found = probe;
    
    function_type_mod_found =
	prog->inherit[i].prog->functions[probe].type_flags &
	    TYPE_MOD_MASK ;
    
    /* Correct function_type_mod_found */
    type_mod = prog->inherit[i].type;
    
    if (function_type_mod_found & TYPE_MOD_PRIVATE)
	type_mod &= ~TYPE_MOD_PUBLIC;
    if (function_type_mod_found & TYPE_MOD_PUBLIC)
	type_mod &= ~TYPE_MOD_PRIVATE;
    function_type_mod_found |= type_mod;
    return 1;
}

INLINE int 
search_for_function(char *name, struct program *prog)
{
    return s_f_f(find_sstring(name), prog);
}
	    
/*
 * Apply a fun 'fun' to the program in object 'ob', with
 * 'num_arg' arguments (already pushed on the stack).
 * If the function is not found, search in the object pointed to by the
 * inherit pointer.
 * If the function name starts with '::', search in the object pointed out
 * through the inherit pointer by the current object. The 'current_object'
 * stores the base object, not the object that has the current function being
 * evaluated. Thus, the variable current_prog will normally be the same as
 * current_object->prog, but not when executing inherited code. Then,
 * it will point to the code of the inherited object. As more than one
 * object can be inherited, the call of function by index number has to
 * be adjusted. The function number 0 in a superclass object must not remain
 * number 0 when it is inherited from a subclass object. The same problem
 * exists for variables. The global variables function_index_offset and
 * variable_index_offset keep track of how much to adjust the index when
 * executing code in the superclass objects.
 *
 * There is a special case when called from the heart beat, as
 * current_prog will be 0. When it is 0, set current_prog
 * to the 'ob->prog' sent as argument.
 *
 * Arguments are always removed from the stack.
 * If the function is not found, return 0 and nothing on the stack.
 * Otherwise, return 1, and a pushed return value on the stack.
 *
 * Note that the object 'ob' can be destructed. This must be handled by
 * the caller of apply().
 *
 * If the function failed to be called, then arguments must be deallocated
 * manually !
 */
	    
#ifdef DEBUG
char debug_apply_fun[30]; /* For debugging */
#endif
	    
unsigned long long globcache_tries = 0, globcache_hits = 0;	    

    
static int
apply_low(char *fun, struct object *ob, int num_arg, int external)
{
    struct program *progp;
    char *sfun;

    /*
     * This object will now be used, and is thus a target for
     * reset later on (when time due).
     */
	    
#ifdef DEBUG
    (void)strncpy(debug_apply_fun, fun, sizeof debug_apply_fun);
    debug_apply_fun[sizeof debug_apply_fun - 1] = '\0';
#endif
    if (*fun == '.')
	goto failure;
	    
    /*
     * If there is a chain of objects shadowing, start with the first
     * of these.
     */
    while (ob->shadowed && ob->shadowed != current_object)
	ob = ob->shadowed;
	 
    sfun = find_sstring(fun);

 retry_for_shadow:
    progp = ob->prog;
	    
#ifdef DEBUG
    if (ob->flags & O_DESTRUCTED)
	fatal("apply() on destructed object\n");
#endif
    if (!(ob->flags & O_CREATED))
	create_object(ob);
    if (ob->flags & O_DESTRUCTED)
	goto failure;
    
    if (s_f_f(sfun, progp)) 
    {
	/* Static or private functions may not be called from outside. */
	if (((ob != current_object || external) && 
	     function_type_mod_found & (TYPE_MOD_STATIC | TYPE_MOD_PRIVATE)) ||
	    (function_type_mod_found & TYPE_MOD_PRIVATE && 
	     function_prog_found != ob->prog))
	    ; /* Do nothing */
	else
        {
	    call_function(ob, function_inherit_found,
			  (unsigned int)function_index_found, num_arg);
	    
	    return 1;
	    
 	}
	    
    }
    
    /* Not found */
    if (ob->shadowing) 
    {
	/*
	 * This is an object shadowing another. The function was not found,
	 * but can maybe be found in the object we are shadowing.
	 */
	ob = ob->shadowing;
	goto retry_for_shadow;
    }
 failure:
    /* Failure. Deallocate stack. */
    pop_n_elems(num_arg);
    return 0;
}

/*
 * Arguments are supposed to be
 * pushed (using push_string() etc) before the call. A pointer to a
 * 'struct svalue' will be returned. It will be a null pointer if the called
 * function was not found. Otherwise, it will be a pointer to a static
 * area in apply(), which will be overwritten by the next call to apply.
 * Reference counts will be updated for this value, to ensure that no pointers
 * are deallocated.
 */
	    
struct svalue *
sapply(char *fun, struct object *ob, int num_arg, int ext)
{
	    
#ifdef DEBUG
    struct svalue *expected_sp;
#endif
    static struct svalue ret_value = { T_NUMBER };
	    
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
    if (fun == NULL) {
	free_svalue(&ret_value);
	return NULL;
    }
#endif

#ifdef TRACE_CODE
    if (TRACEP(TRACE_APPLY)) 
    {
	char buff[1024];
	(void)sprintf(buff,"%s->%s", ob->name, fun);
	do_trace("Apply", "", "\n");
    }
#endif

#ifdef DEBUG
    expected_sp = sp - num_arg;
#endif
    if (!ob || (ob->flags & O_DESTRUCTED)) {
        pop_n_elems(num_arg);
	return 0;
    }
    
    if (apply_low(fun, ob, num_arg, ext) == 0)
	return 0;
    assign_svalue(&ret_value, sp);
    pop_stack();
#ifdef DEBUG
    if (expected_sp != sp)
	fatal("Corrupt stack pointer.\n");
#endif
    return &ret_value;
}
	    
	    
struct svalue *
apply(char *fun, struct object *ob, int num_arg, int ext)
{
    tracedepth = 0;
    return sapply(fun, ob, num_arg, ext);
}
	    
/*
 * This function is similar to apply(), except that it will not
 * call the function, only return object name if the function exists,
 * or 0 otherwise.
 */
char *
function_exists(char *fun, struct object *ob)
{
#ifdef DEBUG
    if (ob->flags & O_DESTRUCTED)
	fatal("function_exists() on destructed object\n");
#endif
    if (*fun == '.')
	return 0;
    if ( search_for_function (fun, ob->prog)
	&& (!(function_type_mod_found & (TYPE_MOD_STATIC|TYPE_MOD_PRIVATE))
	    || current_object == ob) ) 
	return function_prog_found->name;
    /* Not found */
    return 0;
}
	    
/*
 * Call a specific function address in an object. This is done with no
 * frame set up. It is expected that there are no arguments. Returned
 * values are removed.
 */
	    
void 
call_function(struct object *ob, int inh_index, unsigned int fun, int num_arg)
{
    char *cp;
    struct function *funp;
    struct program *progp;
    
    if (inh_index < 0 || /*fun < 0 ||*/ inh_index >= (int)ob->prog->num_inherited ||
	fun >= ob->prog->inherit[inh_index].prog->num_functions)
    {
	/* invalid function */
	pop_n_elems(num_arg);
	push_number(0);
	return;
    }
    progp = ob->prog->inherit[inh_index].prog;
    funp = &progp->functions[fun];
    
    if (funp->type_flags & NAME_PROTOTYPE) /* Cannot happen. */
	return;
    
    push_control_stack(ob, progp, funp);
    csp->ext_call = 1;
    csp->num_local_variables = num_arg;
    current_prog = progp;
    inh_offset = inh_index;
    previous_ob = current_object;
    current_object = ob;
#ifdef DEBUG
    if (current_object->prog->inherit[inh_offset].prog != current_prog)
	fatal("Corrupt inherit offset!\n");
#endif
    cp = setup_new_frame(funp);
    csp->extern_call = 1;
    eval_instruction(cp);
}
	    
/*
 * Get srccode position including runtime errors in included files.
 * 
 */
char *
inner_get_srccode_position(int code, struct lineno *lineno, int lineno_count,
		     char *inc_files, char *name)
{
    static char buff[200];
    struct lineno *lo = lineno, *hi = lineno + lineno_count - 1;
    struct lineno *mid = lo + (hi - lo) / 2;
    int filenum;
    char *filename;

    if (hi->code <= code) {
        filenum = hi->file;

        if (filenum == 0)
            filename = name;
        else {
            filename = inc_files;
            while (filenum > 1) {
                filename += strlen(filename) + 1;
                filenum--;
            }
            filename = strchr(filename, ':');
            filename++;
        }
        (void)sprintf(buff, "/%s Line: %d", filename, hi->lineno);
        return buff;
    }
    
    while (hi - lo > 1) {
        if (mid->code > code)
            hi = mid;
        else
            lo = mid;
        mid = lo + (hi - lo) / 2;
    }
    filenum = lo->file;
    
    if (filenum == 0)
        filename = name;
    else {
        filename = inc_files;
        while (filenum > 1) {
            filename += strlen(filename) + 1;
            filenum--;
        }
        filename = strchr(filename, ':');
        filename++;
    }
    (void)sprintf(buff, "/%s Line: %d", filename, lo->lineno);
    return buff;
}

char *
get_srccode_position(int offset, struct program *progp)
{
    char *ret;

    if (progp == 0)
	return "";
    
#ifdef DEBUG
    if (offset > progp->program_size)
	fatal("Illegal offset %d in object %s\n", offset, progp->name);
#endif

    ret = inner_get_srccode_position(offset, progp->line_numbers,
				     progp->sizeof_line_numbers,
				     progp->include_files,
				     progp->name);

    return ret;
}

/*
 * Write out a trace. If there is an heart_beat(), then return the
 * object that had that heart beat.
 */
char *
dump_trace(int how)
{
    struct control_stack *p;
    char *ret = 0;
#if defined(DEBUG) && defined(TRACE_CODE)
    int last_instructions (void);
#endif
    char *line;

    if (current_prog == 0)
	return 0;
    if (csp < &control_stack[0])
    {
	(void) printf("No trace.\n");
	debug_message("No trace.\n");
	return 0;
    }
#if defined(DEBUG) && defined(TRACE_CODE)
    if (how)
	(void) last_instructions();
#endif
    for (p = &control_stack[0]; p < csp; p++) 
    {
#define FORM "%-15s in /%s\n                   /%s\n                   %s\n"
	line = get_srccode_position((int)p[1].pc, p[1].prog);
	debug_message(FORM,
		      p[0].funp ? p[0].funp->name : "CATCH",
		      p[1].prog->name, p[1].ob->name,
		      line);
	if (p->funp && strcmp(p->funp->name, "heart_beat") == 0)
	    ret = p->ob?p->ob->name:0; /*crash unliked gc*/
    }
    line = get_srccode_position(pc - current_prog->program,
				current_prog);
    debug_message(FORM,
		  p[0].funp ? p[0].funp->name : "CATCH",
		  current_prog->name, current_object->name,
		  line);
    return ret;
}

char *
get_srccode_position_if_any() 
{
    char *ret = "";

    if (current_prog)
	ret = (char *)get_srccode_position(pc - current_prog->program, current_prog);

    return ret;
}
	    
static char *
find_percent(char *str)
{
    for (;;)
    {
	str = strchr(str, '%');
	if (str == 0)
	    return 0;
	if (str[1] != '%')
	    return str;
	str++;
    }
}
	    
static int
inter_sscanf(int num_arg)
{
    char *fmt;		/* Format description */
    char *in_string;	/* The string to be parsed. */
    int number_of_matches;
    char *cp;
    struct svalue *arg = sp - num_arg + 1;
	    
    /*
     * First get the string to be parsed.
     */
    if (arg[0].type != T_STRING)
	bad_arg(1, F_SSCANF, &arg[0]);
    in_string = arg[0].u.string;
    if (in_string == 0)
	return 0;
    /*
     * Now get the format description.
     */
    if (arg[1].type != T_STRING)
	bad_arg(2, F_SSCANF, &arg[1]);
    fmt = arg[1].u.string;
    /*
     * First, skip and match leading text.
     */
    for (cp = find_percent(fmt); fmt != cp; fmt++, in_string++)
    {
	if (in_string[0] == '\0' || fmt[0] != in_string[0])
	    return 0;
    }
    /*
     * Loop for every % or substring in the format. Update num_arg and the
     * arg pointer continuosly. Assigning is done manually, for speed.
     */
    num_arg -= 2;
    arg += 2;
    for (number_of_matches = 0; num_arg > 0;
	 /* LINTED: expression has null effect */
	 number_of_matches++, num_arg--, arg++)
    {
	int i, type, base = 0;
	    
	if (fmt[0] == '\0')
	{
	    /*
	     * We have reached end of the format string.
	     * If there are any chars left in the in_string,
	     * then we put them in the last variable (if any).
	     */
	    if (in_string[0])
	    {
		free_svalue(arg->u.lvalue);
		arg->u.lvalue->type = T_STRING;
		arg->u.lvalue->u.string = make_mstring(in_string);
		arg->u.lvalue->string_type = STRING_MSTRING;
		number_of_matches++;
	    }
	    break;
	}
#ifdef DEBUG
	if (fmt[0] != '%')
	    fatal("Should be a %% now !\n");
#endif
	type = T_STRING;
	if (fmt[1] == 'd') {
	    type = T_NUMBER; base = 10;}
	else if (fmt[1] == 'x') {
	    type = T_NUMBER; base = 0x10;}
	else if (fmt[1] == 'o') {
	    type = T_NUMBER; base = 010;}
	else if (fmt[1] == 'i') {
	    type = T_NUMBER; base = 0;}
	else if (fmt[1] == 'f')
	    type = T_FLOAT;
	else if (fmt[1] != 's')
	    error("Bad type : '%%%c' in sscanf fmt string.\n", fmt[1]);
	fmt += 2;
	/*
	 * Parsing a number is the easy case. Just use strtol() to
	 * find the end of the number.
	 */
	if (type == T_NUMBER)
	{
	    char *tmp = in_string;
	    long long tmp_num;
	    
	    tmp_num = (long long) strtoll(in_string, &in_string, base);
	    if(tmp == in_string)
	    {
		/* No match */
		break;
	    }
	    free_svalue(arg->u.lvalue);
	    arg->u.lvalue->type = T_NUMBER;
	    arg->u.lvalue->u.number = tmp_num;
	    while(fmt[0] && fmt[0] == in_string[0])
		fmt++, in_string++;
	    if (fmt[0] != '%')
	    {
		number_of_matches++;
		break;
	    }
	    continue;
	}
	if (type == T_FLOAT)
	{
	    char *tmp = in_string;
	    double tmp_num;
	    
	    tmp_num = strtod(in_string, &in_string);
	    if(tmp == in_string)
	    {
		/* No match */
		break;
	    }
	    free_svalue(arg->u.lvalue);
	    arg->u.lvalue->type = T_FLOAT;
	    arg->u.lvalue->u.real = tmp_num;
	    while(fmt[0] && fmt[0] == in_string[0])
		fmt++, in_string++;
	    if (fmt[0] != '%')
	    {
		number_of_matches++;
		break;
	    }
	    continue;
	}
	/*
	 * Now we have the string case.
	 */
	cp = find_percent(fmt);
	if (cp == fmt)
	    error("Illegal to have 2 adjacent %%s in fmt string in sscanf.\n");
	if (cp == 0)
	    cp = fmt + strlen(fmt);
	/*
	 * First case: There was no extra characters to match.
	 * Then this is the last match.
	 */
	if (cp == fmt)
	{
	    free_svalue(arg->u.lvalue);
	    
	    arg->u.lvalue->type = T_STRING;
	    arg->u.lvalue->u.string = make_mstring(in_string);
	    arg->u.lvalue->string_type = STRING_MSTRING;
	    number_of_matches++;
	    break;
	}
	for (i = 0; in_string[i]; i++)
	{
	    if (strncmp(in_string+i, fmt, (size_t)(cp - fmt)) == 0)
	    {
		char *match;
		/*
	         * Found a match !
		 */
		match = allocate_mstring((size_t)i);
		(void) strncpy(match, in_string, (size_t)i);
		in_string += i + cp - fmt;
		match[i] = '\0';
		free_svalue(arg->u.lvalue);
		arg->u.lvalue->type = T_STRING;
		arg->u.lvalue->string_type = STRING_MSTRING;
		arg->u.lvalue->u.string = match;
		fmt = cp;	/* Advance fmt to next % */
		break;
	    }
	}
	if (fmt == cp)	/* If match, then do continue. */
	    continue;
	/*
	 * No match was found. Then we stop here, and return
	 * the result so far !
	 */
	break;
    }
    return number_of_matches;
}
	    
/* test stuff ... -- LA */
#ifdef OPCPROF
void
opcdump(void)
{
    int i;
    
    for(i = 0; i < MAXOPC; i++)
	if (opcount[i])
	    (void)fprintf(stderr, "%-20s %12d: %12d\n", get_f_name(i), i,
			  opcount[i]);
}
#endif
	    
/*
 * Reset the virtual stack machine.
 */
void
init_machine() {
    sp = start_of_stack - 1;
    csp = control_stack - 1;
}

void
reset_machine()
{
#if defined(PROFILE_LPC)
    if (csp != control_stack - 1) {
	double now = current_cpu();
	struct program *prog = current_prog;
	last_execution = now;
	csp->frame_cpu += (now - csp->startcpu);
	for (; csp != control_stack - 1; (prog = csp->prog), csp--)
	{
	    double tot_delta = now - csp->frame_start;
	    if (prog)
		update_prog_profile(prog, now, csp->frame_cpu, tot_delta);
	    if (csp->funp) {
		update_func_profile(csp->funp, now, csp->frame_cpu, tot_delta, 1);
	    }
	    if (trace_calls) {
		fprintf(trace_calls_file, "%*s--- %.3f / %.3f\n",
			(csp - control_stack) * 4, "",
			csp->frame_cpu * 1000.0,
			tot_delta * 1000.0);
	    }
	}
	if (trace_calls)
	    putc('\n', trace_calls_file);
    }
#else
    csp = control_stack - 1;
#endif
    pop_n_elems(sp - start_of_stack + 1);
}
	    
#if defined(DEBUG) && defined(TRACE_CODE)
static char *
get_arg(unsigned long a, unsigned long b)
{
    static char buff[10];
    char *from, *to;
    
    from = previous_prog[a]->program + previous_pc[a];
    if (EXTRACT_UCHAR(from) + EFUN_FIRST == F_EXT)
	from++;

    to = previous_prog[b]->program + previous_pc[b];
    if (to - from < 2)
	return "";
    if (to - from == 2)
    {
	(void)sprintf(buff, "%d", from[1]);
	return buff;
    }
    if (to - from == 3)
    {
	short arg;
	((char *)&arg)[0] = from[1];
	((char *)&arg)[1] = from[2];
	(void)sprintf(buff, "%d", arg);
	return buff;
    }
    if (to - from == 5)
    {
	int arg;
	((char *) &arg)[0] = from[1];
	((char *) &arg)[1] = from[2];
	((char *) &arg)[2] = from[3];
	((char *) &arg)[3] = from[4];
	(void)sprintf(buff, "%d", arg);
	return buff;
    }
    return "";
}
	    
int
last_instructions()
{
    unsigned long i;
    i = last;
    do
    {
	if (previous_prog[i])
	    (void)printf("%5lx: %3d%*s %8s %-25s (%d) %s\n",
			 (unsigned long)previous_pc[i],
		   previous_instruction[i],
		   curtracedepth[i],"",
		   get_arg(i, (i+1) % TRACE_SIZE),
		   get_f_name(previous_instruction[i]),
		   stack_size[i] + 1,
			 get_srccode_position(previous_pc[i], previous_prog[i])
			 );
	i = (i + 1) % TRACE_SIZE;
    } while (i != last);
    return last;
}
#endif /* DEBUG && TRACE_CODE */
	    
	    
#ifdef DEBUG
	    
static void
count_inherits(struct program *progp, struct program *search_prog)
{
    int i;
	    
    /* Clones will not add to the ref count of inherited progs */
    if (progp->extra_ref != 1)
	return; 
    for (i = 0; i < progp->num_inherited; i++)
    {
	progp->inherit[i].prog->extra_ref++;
	if (progp->inherit[i].prog == search_prog)
	    (void)printf("Found prog, inherited by %s\n", progp->name);
	count_inherits(progp->inherit[i].prog, search_prog);
    }
}
	    
static void
count_ref_in_vector(struct svalue *svp, int num)
{
    struct svalue *p;
    
    for (p = svp; p < svp + num; p++)
    {
	switch(p->type)
	{
	case T_OBJECT:
	    p->u.ob->extra_ref++;
	    continue;
	case T_POINTER:
	    count_ref_in_vector(&p->u.vec->item[0], p->u.vec->size);
	    p->u.vec->extra_ref++;
	    continue;
	}
    }
}
	    
/*
 * Clear the extra debug ref count for vectors
 */
void
clear_vector_refs(struct svalue *svp, int num)
{
    struct svalue *p;
    
    for (p = svp; p < svp + num; p++)
    {
	switch(p->type)
	{
	case T_POINTER:
	    clear_vector_refs(&p->u.vec->item[0], p->u.vec->size);
	    p->u.vec->extra_ref = 0;
	    continue;
	}
    }
}
	    
/*
 * Loop through every object and variable in the game and check
 * all reference counts. This will surely take some time, and should
 * only be used for debugging.
 */
void
check_a_lot_ref_counts(struct program *search_prog)
{
    extern struct object *master_ob;
    struct object *ob;
    
    /*
     * Pass 1: clear the ref counts.
     */
    ob = obj_list;
    do
    {
	ob->extra_ref = 0;
	ob->prog->extra_ref = 0;
	    
	clear_vector_refs(ob->variables, ob->prog->num_variables +
			  ob->prog->inherit[ob->prog->num_inherited - 1]
			  .variable_index_offset);
	ob = ob->next_all;
    } while (ob != obj_list);

    clear_vector_refs(start_of_stack, sp - start_of_stack + 1);
	    
    /*
     * Pass 2: Compute the ref counts.
     */
	    
    /*
     * List of all objects.
     */
    for (ob = obj_list; ob; ob = ob->next_all)
    {
	ob->extra_ref++;
	count_ref_in_vector(ob->variables, ob->prog->num_variables +
			    ob->prog->inherit[ob->prog->num_inherited - 1]
			    .variable_index_offset);
	ob->prog->extra_ref++;
	if (ob->prog == search_prog)
	    (void)printf("Found program for object %s\n", ob->name);
	/* Clones will not add to the ref count of inherited progs */
	if (ob->prog->extra_ref == 1)
	    count_inherits(ob->prog, search_prog);
    }
	    
    /*
     * The current stack.
     */
    count_ref_in_vector(start_of_stack, sp - start_of_stack + 1);
    update_ref_counts_for_players();
    count_ref_from_call_outs();
    if (master_ob)
	master_ob->extra_ref++;
	    
    if (search_prog)
	return;
	    
    /*
     * Pass 3: Check the ref counts.
     */
    for (ob = obj_list; ob; ob = ob->next_all)
    {
	if (ob->ref != ob->extra_ref)
	    fatal("Bad ref count in object %s, %d - %d\n", ob->name,
		  ob->ref, ob->extra_ref);
	if (ob->prog->ref != ob->prog->extra_ref)
	{
	    check_a_lot_ref_counts(ob->prog);
	    fatal("Bad ref count in prog %s, %d - %d\n", ob->prog->name,
		  ob->prog->ref, ob->prog->extra_ref);
	}
    }
}
	    
#endif /* DEBUG */
	    
#ifdef TRACE_CODE
/* Generate a debug message to the player */
static void
do_trace(char *msg, char *fname, char *post)
{
    char buf[10000], *p;
    char *objname;
	    
    objname = TRACETST(TRACE_OBJNAME) ? (current_object && current_object->name ? current_object->name : "??")  : "";
    (void)sprintf(buf, "*** %d %*s %s %s %s", tracedepth, tracedepth, "", msg, objname, fname);
    p = buf + strlen(buf);
    if (TRACETST(TRACE_TOS)) {
	(void)strcpy(p, string_print_formatted(0, " *sp=%O", 1, sp));
	p += strlen(p);
    }
    (void)strcpy(p, post);
    write_socket(buf, command_giver);
}
#endif

#include "master.t"

void
resolve_master_fkntab()
{
    extern struct object *master_ob;
    struct program *prog = master_ob->prog;

    struct fkntab *tab;

    for (tab = master_fkntab; tab->name; tab++)
    {
	if (search_for_function(tab->name, prog))
	{
	    tab->inherit_index = function_inherit_found;
	    tab->function_index = function_index_found;
	}
	else
	{
	    tab->inherit_index = (unsigned short)-1;
	    tab->function_index = (unsigned short)-1;
	}
    }
}

struct svalue *
apply_master_ob(int fun, int num_arg)
{
    extern struct object *master_ob;
    static struct svalue retval = { T_NUMBER };

    if (s_flag)
	num_mcall++;
    if (!master_ob || (master_fkntab[fun].inherit_index == (unsigned short)-1 &&
	master_fkntab[fun].function_index == (unsigned short)-1))
    {
	pop_n_elems(num_arg);
	return 0; /* No such function */
    }
    call_function(master_ob, master_fkntab[fun].inherit_index,
		  (unsigned int)master_fkntab[fun].function_index, num_arg);

    assign_svalue(&retval, sp);
    pop_stack();
    return &retval;
}

/*EOT*/
	    
/*
 * When an object is destructed, all references to it must be removed
 * from the stack.
 */
void
remove_object_from_stack(struct object *ob)
{
    struct svalue *svp;
    
    for (svp = start_of_stack; svp <= sp; svp++)
    {
	if (svp->type != T_OBJECT)
	    continue;
	if (svp->u.ob != ob)
	    continue;
	free_object(svp->u.ob, "remove_object_from_stack");
	svp->type = T_NUMBER;
	svp->u.number = 0;
    }
}
void
stack_swap_objects(struct object *ob1, struct object *ob2)
{
    struct control_stack *cspi;

    if (current_object == ob1)
	current_object = ob2;
    else if (current_object == ob2)
	current_object = ob1;

    if (previous_ob == ob1)
	previous_ob = ob2;
    else if (previous_ob == ob2)
	previous_ob = ob1;
    
    for (cspi = csp; cspi >= control_stack; cspi--)
    {
	if (cspi->ob == ob1)
	    cspi->ob = ob2;
	else if (cspi->ob == ob2)
	    cspi->ob = ob1;
    
	if (cspi->prev_ob == ob1)
	    cspi->prev_ob = ob2;
	else if (cspi->prev_ob == ob2)
	    cspi->prev_ob = ob1;
    }
    /* FIXME: Process error-stack and data-stack as well. */
}    

#ifdef TRACE_CODE
static int
strpref(char *p, char *s)
{
    while (*p)
	if (*p++ != *s++)
	    return 0;
    return 1;
}
#endif

static void
call_efun(int instruction, int numa)
{
    if (instruction < EFUN_FIRST || instruction > EFUN_LAST)
	fatal("Undefined instruction %s (%d)\n", get_f_name(instruction),
	      instruction);
    instruction -= EFUN_FIRST;
    if (instrs[instruction].min_arg != instrs[instruction].max_arg) {
	if (instrs[instruction].min_arg != -1 && 
	    instrs[instruction].min_arg > numa)
	    error("Too few arguments to efun.\n");
	if (instrs[instruction].max_arg != -1 && 
	    instrs[instruction].max_arg < numa)
	    error("Too many arguments to efun.\n");
    } else {
	if (numa != instrs[instruction].min_arg) {
	    (void)fprintf(stderr, "call_efun %d numa=%d min_arg=%d\n",
			  instruction, numa, instrs[instruction].min_arg);
	    error("Bad number of arguments to efun.\n");
	}
    }
    if (numa > 0) {
	int type1 = (sp-numa+1)->type, type2 = (sp-numa+2)->type;
	if (instrs[instruction].type[0] != 0 &&
	    (instrs[instruction].type[0] & type1) == 0) {
	    bad_arg(1, instruction + EFUN_FIRST, sp-numa+1);
	}
	if (numa > 1) {
	    if (instrs[instruction].type[1] != 0 &&
		(instrs[instruction].type[1] & type2) == 0)
		bad_arg(2, instruction + EFUN_FIRST, sp-numa+2);
	}
    }
    efun_table[instruction](numa);
}
