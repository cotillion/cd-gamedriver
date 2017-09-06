#ifndef INTERPRET_H
#define INTERPRET_H

#include <setjmp.h>

#define push_svalue(val) \
    { \
	if (sp + 1 >= &start_of_stack[EVALUATOR_STACK_SIZE]) \
	    error("Stack overflow\n"); \
	sp++; \
	assign_svalue_no_free(sp, val); \
    }

union u {
    char *string;
    long long number;
    double real;
    struct object *ob;
    struct vector *vec;
    struct svalue *lvalue;
    struct mapping *map;
    struct closure *func;
};

/*
 * The value stack element.
 * If it is a string, then the way that the string has been allocated differ,
 * wich will affect how it should be freed.
 */
struct svalue {
    short type;
    short string_type;
    union u u;
};

extern struct svalue *sp;
extern struct svalue start_of_stack[];

#define T_INVALID	0x0
#define T_LVALUE	0x1
#define T_NUMBER	0x2
#define T_STRING	0x4
#define T_POINTER	0x8
#define T_OBJECT	0x10
#define T_MAPPING	0x20
#define T_FLOAT         0x40
#define T_FUNCTION	0x80

#define STRING_MSTRING	0	/* Allocated by malloc() */
#define STRING_SSTRING	1	/* Allocated by the shared string library */
#define STRING_CSTRING	2	/* Do not has to be freed at all */

struct vector {
    unsigned int size;
    unsigned int ref;
#ifdef DEBUG
    int extra_ref;
#endif
    struct svalue item[1];
};

#define ALLOC_VECTOR(nelem) \
    (struct vector *)xalloc(sizeof (struct vector) + \
			    sizeof(struct svalue) * (nelem - 1))

struct lnode_def;

/*
 * Function stuff.
 */
struct closure {
#ifdef FUNCDEBUG
    int magic;
#define FUNMAGIC 0xdeadbeef
    struct closure *next;
    struct object *from;
#endif
    unsigned int ref;           /* reference counter */
    char funtype;
#define FUN_LFUN 1
#define FUN_SFUN 2
#define FUN_EFUN 3
#define FUN_LFUNO 4
#define FUN_COMPOSE 5		/* used for compositions */
#define FUN_EMPTY 6		/* used for empty argument slots */
#define FUN_LFUN_NOMASK 7       /* used for nomask functions */
    unsigned short funno, funinh; /* function no, and inherit no. used in call */
    struct object *funobj;	/* object where function is, or 0 */
    struct vector *funargs;	/* function arguments, or 0 */
    /* "empty" argument slots in the argument array contain
       function nodes with the FUN_EMPTY tag */
};

/*
 * Control stack element.
 * 'prog' is usually same as 'ob->prog' (current_object), except when
 * when the current function is defined by inheritance.
 * The pointer, csp, will point to the values that will be used at return.
 */
struct control_stack {
    struct object *ob;		/* Current object */
    struct object *prev_ob;	/* Save previous object */
    struct program *prog;	/* Current program */
    int num_local_variables;	/* Local + arguments */
    unsigned pc;
    unsigned pc_save;
    struct svalue *fp;
    int extern_call;		/* Flag if evaluator should return */
    struct function *funp;	/* Only used for tracebacks */
    int inh_offset;
    char ext_call;
#if defined(PROFILE_LPC)
    double startcpu;
    double frame_cpu;
    double frame_start;
#endif
};

/*
 * The following structure is used to create a linked-lists of
 * exception frames threaded through the C stack.  When an error
 * recovery context is desired, a local exception frame is allocated
 * on the stack,  To create an error recovery context, initialize
 * the local exception frame and append it to the list.  To restore
 * the previous context, remove the frame from the list.
 */
struct gdexception {
    struct gdexception *e_exception;
    int			e_catch;
    jmp_buf		e_context;
};

/*
 * Boolean Type
 */

typedef int	bool_t;

#ifndef FALSE
#define	FALSE	0
#endif
#ifndef TRUE
#define	TRUE	1
#endif

extern struct gdexception *exception;
extern struct svalue const0, const1, constempty;

extern int variable_index_found;
extern int variable_inherit_found;    
extern int variable_type_mod_found;

extern int function_index_found;
extern struct program *function_prog_found;
extern unsigned short function_type_mod_found;
extern int function_inherit_found;
extern struct control_stack *csp;	/* Points to last element pushed */

#define INCREF(x) if (x) x++
#define DECREF(x) if (x) x--

void push_pop_error_context(int push);
void pop_stack(void);
int search_for_function(char *name, struct program *prog);
void free_closure(struct closure *f);
void push_control_stack(struct object*, struct program *, struct function *funp);
void pop_control_stack(void);
void push_vector(struct vector*i, bool_t);
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void clear_closure_cache(void);
#endif

#endif
