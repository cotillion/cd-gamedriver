%{
#line 3 "prelang.y"

/* The first line is to give proper line number references. Please mail me
 * if your compiler complains about it.
 */

/*
 * This is the grammar definition of LPC. The token table is built
 * automatically by make_func. The lang.y is constructed from this file,
 * the generated token list and post_lang.y. The reason of this is that there
 * is no #include-statment that yacc recognizes.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"
#include "lint.h"
#include "interface.h"
#include "object.h"
#include "instrs.h"
#include "incralloc.h"
#include "mstring.h"
#include "simulate.h"
#include "backend.h"
#include "lex.h"
#include "efun_table.h"

/* This is to see to it that we always go through xalloc() in main.c
   even from within the yacc-parser
*/
#ifdef malloc
#undef malloc
#endif
#define malloc xalloc

#define YYMAXDEPTH	600

#define BREAK_ON_STACK		0x40000
#define BREAK_FROM_CASE		0x80000

/* make shure that this struct has a size that is a power of two */
struct case_heap_entry { long long key; short addr; short line; };
#define CASE_HEAP_ENTRY_ALIGN(offset) offset &= -sizeof(struct case_heap_entry)

static struct mem_block mem_block[NUMAREAS];

static int has_inherited;

/*
 * Some good macros to have.
 */

#define BASIC_TYPE(e,t) ((e) == TYPE_ANY ||\
			 (e) == (t) ||\
			 (t) == TYPE_ANY)

#define TYPE(e,t) (BASIC_TYPE((e) & TYPE_MASK, (t) & TYPE_MASK) ||\
		   (((e) & TYPE_MOD_POINTER) && ((t) & TYPE_MOD_POINTER) &&\
		    BASIC_TYPE((e) & (TYPE_MASK & ~TYPE_MOD_POINTER),\
			       (t) & (TYPE_MASK & ~TYPE_MOD_POINTER))))

#define FUNCTION(n) ((struct function *)mem_block[A_FUNCTIONS].block + (n))
#define VARIABLE(n) ((struct variable *)mem_block[A_VARIABLES].block + (n))
#define INHERIT(n)  ((struct inherit *)mem_block[A_INHERITS].block + (n))

#define align(x) ( ((x) + (sizeof(double)-1) )  &  ~(sizeof(double)-1) )

/*
 * If the type of the function is given, then strict types are
 * checked and required.
 */
static int exact_types;
extern int pragma_strict_types;	/* Maintained by lex.c */
extern int pragma_save_binary;	/* Save this in the binary shadow dir */
extern int pragma_no_inherit;
extern int pragma_no_shadow;
extern int pragma_no_clone;
extern struct object *auto_ob;

static int true_varargs;

extern int total_num_prog_blocks, total_prog_block_size;

extern int num_parse_error;
extern int d_flag;

static int current_break_address;
static int current_continue_address;
static int current_case_address;
static int current_case_number_heap;
static int current_case_string_heap;
static int try_level, break_try_level, continue_try_level;
#define SOME_NUMERIC_CASE_LABELS 0x40000
#define NO_STRING_CASE_LABELS    0x80000
static int zero_case_label;
static int current_type;

static char *get_type_name(int);

/*
 * There is always function starting at address 0, which will execute
 * the initialization code. This code is spread all over the program,
 * with jumps to next initializer. The next variable keeps track of
 * the previous jump. After the last initializer, the jump will be changed
 * into a return(0) statement instead.
 *
 * A function named '.CTOR' will be defined, which will contain the
 * initialization code. If there was no initialization code, then the
 * function will not be defined. That is the usage of the
 * first_last_initializer_end variable.
 *
 * When inheriting from another object, a call will automatically be made
 * to call .CTOR in that code from the current .CTOR.
 */
static int last_initializer_end;
static int first_last_initializer_end;

void epilog (void);
static int check_declared (char *str);
static void prolog (void);
static void push_address (void);
static int pop_address (void);
static int make_label (void);
static void transfer_init_control (void);
static char *get_two_types (int, int);
static int verify_declared (char *);
static int handle_function_id (char *);
static void copy_inherits (struct program *, int, char *);
static int check_inherits(struct program *);
static int search_for_ext_function(char *, struct program *);

static struct program NULL_program; /* marion - clean neat empty struct */

void init_lineno_info(void);
void end_lineno_info(void);
static void push_init_arg_address (int);
static void clear_init_arg_stack (void);
void free_all_local_names (void);
int lookup_local_name(char *);
int add_local_name (char *, int);
void smart_log (char *, int, char *);
extern int yylex (void);
void type_error (char *, int);
char *inner_get_srccode_position(int code, struct lineno *lineno, int lineno_count,
    char *inc_files, char *name);
int search_for_function(char *name, struct program *prog);
int handle_include(char *name, int ignore_errors);

INLINE static unsigned mem_block_size(int, int);

extern int current_line;
/*
 * 'inherit_file' is used as a flag. If it is set to a string
 * after yyparse(), this string should be loaded as an object,
 * and the original object must be loaded again.
 */
extern char *current_file, *inherit_file;

/*
 * The names and types of arguments and auto variables.
 */
static unsigned short type_of_locals[MAX_LOCAL];
static int local_blockdepth[MAX_LOCAL];
static char *local_names[MAX_LOCAL];
static int current_number_of_locals = 0;
static int max_number_of_locals = 0;

/*
 * The types of arguments when calling functions must be saved,
 * to be used afterwards for checking. And because function calls
 * can be done as an argument to a function calls,
 * a stack of argument types is needed. This stack does not need to
 * be freed between compilations, but will be reused.
 */
struct mem_block type_of_arguments;

struct program *prog;	/* Is returned to the caller of yyparse */

/*
 * Compare two types, and return true if they are compatible.
 */
static int 
compatible_types(int t1, int t2)
{
    if (t1 == TYPE_UNKNOWN || t2 == TYPE_UNKNOWN)
	return 0;
    if (t1 == t2)
	return 1;
    if ((t1|TYPE_MOD_NO_MASK|TYPE_MOD_STATIC|TYPE_MOD_PRIVATE|TYPE_MOD_PUBLIC)
	== (t2|TYPE_MOD_NO_MASK|TYPE_MOD_STATIC|TYPE_MOD_PRIVATE|TYPE_MOD_PUBLIC))
	return 1;
    if (t1 == TYPE_ANY || t2 == TYPE_ANY)
	return 1;
    if ((t1 & TYPE_MOD_POINTER) && (t2 & TYPE_MOD_POINTER)) {
	if ((t1 & TYPE_MASK) == (TYPE_ANY|TYPE_MOD_POINTER) ||
	    (t2 & TYPE_MASK) == (TYPE_ANY|TYPE_MOD_POINTER))
	    return 1;
    }
    if (t1 == TYPE_MAPPING)
        return 1;
    return 0;
}

/*
 * Add another argument type to the argument type stack
 */
INLINE
static void 
add_arg_type(unsigned short type)
{
    struct mem_block *mbp = &type_of_arguments;
    while (mbp->current_size + sizeof type > mbp->max_size) {
	mbp->max_size <<= 1;
	mbp->block = realloc(mbp->block, (size_t)mbp->max_size);
    }
    (void)memcpy(mbp->block + mbp->current_size, &type, sizeof type);
    mbp->current_size += sizeof type;
}

/*
 * Pop the argument type stack 'n' elements.
 */
INLINE
static void 
pop_arg_stack(int n)
{
    type_of_arguments.current_size -= sizeof (unsigned short) * n;
}

/* Get a pointer to first argument when there are 'n' arguments in total. */
INLINE
static unsigned short*
get_argument_ptr(int n)
{
  return &((unsigned short *)
	   (type_of_arguments.block + type_of_arguments.current_size))[-n];
}
/*
 * Get type of argument number 'arg', where there are
 * 'n' arguments in total in this function call. Argument
 * 0 is the first argument.
 */
INLINE
int 
get_argument_type(int arg, int n)
{
    return
	((unsigned short *)
	 (type_of_arguments.block + type_of_arguments.current_size))[arg - n];
}

INLINE
static void 
add_to_mem_block(int n, char *data, int size)
{
    struct mem_block *mbp = &mem_block[n];
    while (mbp->current_size + size > mbp->max_size) {
	mbp->max_size <<= 1;
	mbp->block = realloc(mbp->block, (size_t)mbp->max_size);
    }
    (void)memcpy(mbp->block + mbp->current_size, data, (size_t)size);
    mbp->current_size += size;
}

INLINE static unsigned
mem_block_size(n, size)
    int n;
    int size;
{
    return mem_block[n].current_size / size;
}

INLINE static void 
ins_byte(char b)
{
    add_to_mem_block(A_PROGRAM, &b, 1);
}

static INLINE void 
upd_byte(int offset, char l)
{
    mem_block[A_PROGRAM].block[offset] = l;
}

static INLINE char
read_byte(int offset)
{
    return mem_block[A_PROGRAM].block[offset];
}

/*
 * Store a 2 byte number. It is stored in such a way as to be sure
 * that correct byte order is used, regardless of machine architecture.
 * Also beware that some machines can't write a word to odd addresses.
 */
static INLINE void 
ins_short(short l)
{
    add_to_mem_block(A_PROGRAM, (char *)&l + 0, 1);
    add_to_mem_block(A_PROGRAM, (char *)&l + 1, 1);
}

static INLINE void 
upd_short(int offset, short l)
{
    mem_block[A_PROGRAM].block[offset + 0] = ((char *)&l)[0];
    mem_block[A_PROGRAM].block[offset + 1] = ((char *)&l)[1];
}

static INLINE short 
read_short(int offset)
{
    short l;

    ((char *)&l)[0] = mem_block[A_PROGRAM].block[offset + 0];
    ((char *)&l)[1] = mem_block[A_PROGRAM].block[offset + 1];
    return l;
}

static INLINE void
ins_mem(void *data, size_t n)
{
   add_to_mem_block(A_PROGRAM, data, n);
}

/*
 * Store a 4 byte number. It is stored in such a way as to be sure
 * that correct byte order is used, regardless of machine architecture.
 */
static INLINE void 
ins_long(int l)
{
    add_to_mem_block(A_PROGRAM, (char *)&l+0, 1);
    add_to_mem_block(A_PROGRAM, (char *)&l+1, 1);
    add_to_mem_block(A_PROGRAM, (char *)&l+2, 1);
    add_to_mem_block(A_PROGRAM, (char *)&l+3, 1);
}

static INLINE void
ins_long_long(long long ll)
{
    ins_mem(&ll, sizeof(ll));
}

/*
 * Return 1 on success, 0 on failure.
 */
static int 
defined_function(char *s)
{
    int offset;
    int inh;
    struct function *funp;
    char *super_name = 0;
    char *sub_name;
    char *real_name;
    char *search;

    real_name = strrchr(s, ':') + 1;
    sub_name = strchr(s, ':') + 2;
    
    real_name = (find_sstring((real_name == (char *)1) ? s : real_name));
    if(!real_name) {
        return 0;
    }
    if (sub_name == (char *)2)
	for (offset = 0; offset < mem_block[A_FUNCTIONS].current_size;
	     offset += sizeof (struct function)) 
	    {
		funp = (struct function *)&mem_block[A_FUNCTIONS].block[offset];
		/* Only index, prog, and type will be defined. */
		if (real_name == funp->name) 
		    {
			function_index_found = offset / sizeof (struct function);
			function_prog_found = 0;
			function_type_mod_found = funp->type_flags & TYPE_MOD_MASK;
			function_inherit_found =
			    mem_block[A_INHERITS].current_size / 
				sizeof(struct inherit);
			return 1;
		    }
	    }
    else
	if (sub_name - s > 2)
	{
	    super_name = xalloc((size_t)(sub_name - s - 1));
	    (void)memcpy(super_name, s, (size_t)(sub_name - s - 2));
	    super_name[sub_name - s - 2] = 0;
	    if (strcmp(super_name, "this") == 0)
		return defined_function(sub_name);
	}
	else
	    s = sub_name;
    
    /* Look for the function in the inherited programs
	*/

    for (inh = mem_block[A_INHERITS].current_size / sizeof (struct inherit) - 1;
          inh >= 0; inh -= ((struct inherit *)(mem_block[A_INHERITS].block))[inh].prog->
	 num_inherited)
    {
	if (super_name &&
	    strcmp(super_name, ((struct inherit *)(mem_block[A_INHERITS].block))[inh].name) == 0)
	    search = sub_name;
	else
	    search = s;
        if (search_for_ext_function (search,
	    ((struct inherit *)(mem_block[A_INHERITS].block))[inh].prog))
	{
	    /* Adjust for inherit-type */
	    int type = ((struct inherit *)mem_block[A_INHERITS].block)[inh].type;
	    
	    if (function_type_mod_found & TYPE_MOD_PRIVATE)
		type &= ~TYPE_MOD_PUBLIC;
	    if (function_type_mod_found & TYPE_MOD_PUBLIC)
		type &= ~TYPE_MOD_PRIVATE;
            function_type_mod_found |= type & TYPE_MOD_MASK;

	    function_inherit_found += inh -
		(((struct inherit *)(mem_block[A_INHERITS].block))[inh].prog->
		 num_inherited - 1);
	    
	    return 1;
	}
    }
    return 0;
}

/*
 * A mechanism to remember addresses on a stack. The size of the stack is
 * defined in config.h.
 */
static int comp_stackp;
static int comp_stack[COMPILER_STACK_SIZE];

static INLINE void 
push_address()
{
    if (comp_stackp >= COMPILER_STACK_SIZE) {
	yyerror("Compiler stack overflow");
	comp_stackp++;
	return;
    }
    comp_stack[comp_stackp++] = mem_block[A_PROGRAM].current_size;
}

static INLINE int
get_address(void)
{
    return mem_block[A_PROGRAM].current_size;
}

static INLINE void 
push_explicit(int address)
{
    if (comp_stackp >= COMPILER_STACK_SIZE) {
	yyerror("Compiler stack overflow");
	comp_stackp++;
	return;
    }
    comp_stack[comp_stackp++] = address;
}

static INLINE int 
pop_address()
{
    if (comp_stackp == 0)
	fatal("Compiler stack underflow.\n");
    if (comp_stackp > COMPILER_STACK_SIZE) {
	--comp_stackp;
	return 0;
    }
    return comp_stack[--comp_stackp];
}

static INLINE void
add_jump(void)
{
    int offset;

    offset = mem_block[A_PROGRAM].current_size;
    add_to_mem_block(A_JUMPS, (char *)&offset, sizeof (offset));
}

static void 
define_new_function(char *name, char num_arg, unsigned char num_local,
		    int offset, int type_flags, char first_default)
{
    struct function fun;
    struct function *funp;
    unsigned short argument_start_index;
 
    if (defined_function (name)) 
    {
        /* The function is already defined.
         *   If it was defined by inheritance, make a new definition,
         *   unless nomask applies.
         *   If it was defined in the current program, use that definition.
         */
         /* Point to the function definition found 
	  */
        if (function_prog_found)
	{
	    funp = &function_prog_found->functions[function_index_found];
	}
	else
	    funp = FUNCTION(function_index_found);
  
	/* If it was declared in the current program, and not a prototype,
	 * it is a double definition. 
	 */
	if (!(funp->type_flags & NAME_PROTOTYPE) &&
	    !function_prog_found) 
	{
	    char buff[500];

	    (void)snprintf(buff, sizeof(buff), "Redeclaration of function %s", name);
	    yyerror (buff);
	    return;
	}

	/* If neither the new nor the old definition is a prototype,
	 * it must be a redefinition of an inherited function.
	 * Check for nomask. 
	 */
	if ((funp->type_flags & TYPE_MOD_NO_MASK) &&
	    !(funp->type_flags & NAME_PROTOTYPE))
	{
	    char buff[500];

	    (void)snprintf(buff, sizeof(buff), "Illegal to redefine nomask function %s", name);
	    yyerror (buff);
	    return;
	}

	/* Check types 
	 */
	if (exact_types && 
	    ((funp->type_flags & TYPE_MASK) != TYPE_UNKNOWN))
	{
	    if (funp->num_arg != num_arg && 
		!(funp->type_flags & TYPE_MOD_VARARGS)) 
	    {
	        yyerror("Incorrect number of arguments");
		return;
	    }
/*
 * This is just a nuisance! /JnA

	    else if (!(funp->type_flags & NAME_STRICT_TYPES)) 
	    {
	        yyerror("Function called not compiled with type testing");
		return;
	    }
*/

#if 0
            else 
	    {
	        int i;
		/* Now check argument types
		 */
		for (i=0; i < num_arg; i++) 
		{
		}
	    }
#endif
	}
	/* If it is a prototype for a function that has already been defined,
	 * we don't need it. 
	 */
	if ((type_flags & NAME_PROTOTYPE) && !function_prog_found)
	    return;

	/* If the function was defined in an inherited program, we need to
	 * make a new definition here. 
	 */
	if (function_prog_found) {
	    funp = &fun;
	}
    }
    else { /* Function was not defined before, we need a new definition */
        funp = &fun;
    }

#ifdef PROFILE_LPC
    funp->num_calls = 0;
    funp->time_spent = 0;
    funp->tot_time_spent = 0;
    funp->avg_calls = 0;
    funp->avg_time = 0;
    funp->avg_tot_time = 0;
    funp->last_call_update = 0;
#endif
    funp->offset = offset;
    funp->type_flags = type_flags;
    funp->num_arg = num_arg;
    funp->num_local = num_local;
    funp->first_default = first_default;
    funp->hash_idx = -1;
    if (exact_types)
        funp->type_flags |= NAME_STRICT_TYPES;

    if (!exact_types || num_arg == 0)
	argument_start_index = INDEX_START_NONE;
    else 
    {
	int i;
	/*
	 * Save the start of argument types.
	 */
	argument_start_index =
	    mem_block[A_ARGUMENT_TYPES].current_size /
		sizeof (unsigned short);
	for (i=0; i < num_arg; i++)
	    add_to_mem_block(A_ARGUMENT_TYPES, (char *)&type_of_locals[i],
			     sizeof type_of_locals[i]);
    }
    if (funp == &fun) 
    {
	funp->name = make_sstring(name);
        add_to_mem_block (A_FUNCTIONS, (char *)&fun, sizeof fun);
	add_to_mem_block(A_ARGUMENT_INDEX, (char *)&argument_start_index,
                    sizeof argument_start_index);
    }
    else
    {
	(void)memcpy(&mem_block[A_ARGUMENT_INDEX].
		     block[function_index_found * sizeof(argument_start_index)],
		     (char *)&argument_start_index, sizeof(argument_start_index));
    }
    return;
}
 
static INLINE int 
is_simul_efun (char *name)
{

    if (simul_efun_ob != 0 && search_for_function (name, simul_efun_ob->prog) &&
	!(function_type_mod_found & (TYPE_MOD_PRIVATE | TYPE_MOD_STATIC)))
        return 1;
    return 0;
}  

static void 
define_variable(char *name, int type)
{
    struct variable dummy;
    int n;

    n = check_declared(name);
    if (n != -1 && (n & TYPE_MOD_NO_MASK))
    {
	char *p = (char *)alloca(80 + strlen(name));
	(void)sprintf(p, "Illegal to redefine 'nomask' variable \"%s\"", name);
	yyerror(p);
    }

    dummy.name = make_sstring(name);
    dummy.type = type;
    variable_index_found = mem_block_size(A_VARIABLES,sizeof(struct variable));
    variable_inherit_found = 255;
    add_to_mem_block(A_VARIABLES, (char *)&dummy, sizeof dummy);
}

unsigned short
store_prog_string(char *str)
{
    unsigned short addr;
    int i;

    for (i = mem_block[A_STRTAB].current_size - sizeof(short);
	 i >= 0; i -= sizeof(short))
    {
	char *str2;
	unsigned short offset;
	((char *)&offset)[0] = mem_block[A_STRTAB].block[i];
	((char *)&offset)[1] = mem_block[A_STRTAB].block[i + 1];
	str2 = mem_block[A_RODATA].block + offset;
	if (strcmp(str, str2) == 0)
	    return offset;
    }
    if (mem_block[A_RODATA].current_size >= 0x10000)
    {
	yyerror("Too large rodata segment!\n");
	mem_block[A_RODATA].current_size = 0;
    }
    addr = mem_block[A_RODATA].current_size;

    add_to_mem_block(A_STRTAB, (char *)&addr, sizeof(addr));
    add_to_mem_block(A_RODATA, str, (int)strlen(str) + 1);
    return addr;
}

struct label
{
    unsigned short address;
    unsigned short link;
};

static int
make_label()
{
    static struct label lbl = { (unsigned short)-1, (unsigned short)-1};
    int ret;

    ret = mem_block[A_LABELS].current_size / sizeof(struct label);
    add_to_mem_block(A_LABELS, (char *)&lbl, sizeof(lbl));
    return ret;
}

static void
ins_label(int lbl)
{
    struct label *l;
    unsigned short here;

    here = mem_block[A_PROGRAM].current_size;
    l = &((struct label *)mem_block[A_LABELS].block)[lbl];
    if (l->address != (unsigned short)-1)
	ins_short(l->address);
    else
    {
	ins_short(l->link);
	l->link = here;
    }
}

static void
set_label(int lbl, unsigned short addr)
{
    struct label *l;
    unsigned short link1, next;
    /*char *pgm = mem_block[A_PROGRAM].block;*/

    l = ((struct label *)mem_block[A_LABELS].block) + lbl;
    l->address = addr;
    for (link1 = l->link; link1 != (unsigned short)-1; link1 = next)
    {
	next = read_short(link1);
	upd_short(link1, addr);
    }
    l->link = (unsigned short)-1;
}


static INLINE long long cmp_case_keys(struct case_heap_entry *entry1,
				struct case_heap_entry *entry2, int is_str)
{
    if (is_str)
	return strcmp(mem_block[A_RODATA].block + (unsigned short)entry1->key,
		      mem_block[A_RODATA].block + (unsigned short)entry2->key);
    else
	return entry1->key - entry2->key;
}

void 
add_to_case_heap(int block_index, struct case_heap_entry *entry,
		 struct case_heap_entry *entry2)
{
    int current_heap;
    struct case_heap_entry *heap_top, *heap_entry;
    int is_str;
    int from, to, size;


    if (block_index == A_CASE_NUMBERS )
    {
        current_heap = current_case_number_heap;
	is_str = 0;
    }
    else
    {
	current_heap = current_case_string_heap;
	is_str = 1;
    }

    if (entry2 && cmp_case_keys(entry, entry2, is_str) > 0)
	return;

    heap_top = (struct case_heap_entry *)
	(mem_block[block_index].block +
	 mem_block[block_index].current_size);

    heap_entry = (struct case_heap_entry *)
	(mem_block[block_index].block +
	 current_heap);
    
    for (; heap_entry < heap_top; heap_entry++)
    {
	if (cmp_case_keys(heap_entry, entry, is_str) > 0)
	    break;
	if (heap_entry->addr == -1)
	{
	    if (cmp_case_keys(++heap_entry, entry, is_str) >= 0)
	    {
		/* Duplicate case label! */
		char buff[100];

		(void)sprintf(buff, "Duplicate case label (line %d)",
			heap_entry->line);
		yyerror(buff);
		break;
	    }
	}
    }
    
    if (heap_entry < heap_top &&
	(!cmp_case_keys(heap_entry, entry, is_str) ||
	 (entry2 && (cmp_case_keys(entry2, heap_entry, is_str) >= 0))))
    {
	/* Duplicate case label! */
	char buff[100];

	(void)sprintf(buff, "Duplicate case label (line %d)",
		heap_entry->line);
	yyerror(buff);
    }
    
	
    to = ((char *)(heap_entry + 1 + (entry2 != NULL))) -
	mem_block[block_index].block;
    from = ((char *)heap_entry) - mem_block[block_index].block;
    size = (heap_top - heap_entry) * sizeof(*entry);

    add_to_mem_block(block_index, (char *)entry, sizeof(*entry));
    if (entry2)
	add_to_mem_block(block_index, (char *)entry2, sizeof(*entry2));
    
    if (heap_entry != heap_top)
    {
	(void)memmove(mem_block[block_index].block + to,
		      mem_block[block_index].block + from, (size_t)size);
	(void)memcpy(mem_block[block_index].block + from, entry, sizeof(*entry));
	if (entry2)
	    (void)memcpy(mem_block[block_index].block + from + sizeof(*entry),
			 entry2, sizeof(*entry));
    }
}

/*
 * Arrange a jump to the current position for the initialization code
 * to continue.
 */
static void
transfer_init_control() 
{
    if (mem_block[A_PROGRAM].current_size - 2 == last_initializer_end)
	mem_block[A_PROGRAM].current_size -= 3;
    else 
    {
	/*
	 * Change the address of the last jump after the last
	 * initializer to this point.
	 */
	upd_short(last_initializer_end,
		  (short)mem_block[A_PROGRAM].current_size);
    }
}

#define DEREFSIZE 256
static int deref_stack[DEREFSIZE];
static int deref_index;

void add_new_init_jump(void);
static int init_arg_stack[256];
static int init_arg_stack_index;

static void INLINE
clear_init_arg_stack()
{
    init_arg_stack_index = 0;
}

static void INLINE
push_init_arg_address(int address)
{
    init_arg_stack[init_arg_stack_index++] = address;
}

static void ins_f_byte(unsigned int);

/* ARGSUSED1 */
void
dump_init_arg_table(int arg)
{
    int i;
#if defined(DEBUG)
    if (num_parse_error == 0 && init_arg_stack_index != arg)
	fatal("Not correct number of init addresses!\n");
#endif
    for (i = 0; i < init_arg_stack_index; i++)
	ins_short((short)init_arg_stack[i]);
}

%}

/*
 * These values are used by the stack machine, and can not be directly
 * called from LPC.
 */
%token F_EXT F_JUMP F_JUMP_WHEN_ZERO F_JUMP_WHEN_NON_ZERO F_SKIP_NZ
%token F_POP_VALUE F_DUP
%token F_CALL_NON_VIRT F_CALL_VIRT
%token F_PUSH_IDENTIFIER_LVALUE F_PUSH_LOCAL_VARIABLE_LVALUE
%token F_PUSH_INDEXED_LVALUE F_INDIRECT F_INDEX
%token F_CONST0 F_CONST1
%token F_CALL_VAR F_BUILD_CLOSURE F_PAPPLY
/*
 * These are the predefined functions that can be accessed from LPC.
 */

%token F_IDENTIFIER
%token F_RETURN F_STRING F_FLOATC
%token F_INC F_DEC
%token F_POST_INC F_POST_DEC
%token F_NUMBER F_ASSIGN F_ADD F_SUBTRACT F_MULTIPLY
%token F_DIVIDE F_LT F_GT F_EQ F_GE F_LE
%token F_NE
%token F_ADD_EQ F_SUB_EQ F_DIV_EQ F_MULT_EQ
%token F_NEGATE
%token F_SWITCH
%token F_SSCANF F_PARSE_COMMAND F_STRING_DECL F_LOCAL_NAME
%token F_MOD F_MOD_EQ
%token F_STATIC
%token F_ARROW F_AGGREGATE F_M_AGGREGATE
%token F_COMPL F_AND F_AND_EQ F_OR F_OR_EQ F_XOR F_XOR_EQ
%token F_LSH F_LSH_EQ F_RSH F_RSH_EQ F_NOT
%token F_TRY F_END_TRY F_FOREACH F_FOREACH_M
%token F_CATCH F_END_CATCH F_CALL_C F_CALL_SIMUL
%token F_RANGE F_THROW
