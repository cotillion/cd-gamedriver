
/*
 * A compiled program consists of several data blocks, all allocated
 * contiguos in memory to enhance the working set. At the compilation,
 * the blocks will be allocated separately, as the final size is
 * unknow. When compilation is done, the blocks will be copied into
 * the one big area.
 *
 * There are 5 different blocks of information for each program:
 * 1. The program itself. Consists of machine code instructions for a virtual
 *    stack machine. The size of the program must not be bigger than
 *    65535 bytes, as 16 bit pointers are used. Who would ever need a bigger
 *    program :-)
 * 2. Function names. All local functions that has been defined or called,
 *    with the address of the function in the program. Inherited functions
 *    will be found here too, with information of how far up the inherit
 *    chain that the function was defined.
 * 3. String table. All strings used in the program. They are all pointers
 *    into the shared string area. Thus, they are easily found and deallocated
 *    when the object is destructed.
 * 4. Table of variable names. They all point into the shared string table.
 * 5. Line number information. A table which tells at what address every
 *    line belongs to. The table has the same number of entries as the
 *    programs has source lines. This is used at errors, to find out the
 *    line number of the error.
 * 6. List of inherited objects.
 *    Includes indirect inherits.
 *    F_CALL_DOWN gets an index to this table.
 */
#include "config.h"

struct reloc 
{
    char *name;
    int value;
    unsigned short address;
    char type;
    char modifier;
};

struct cfun_desc
{
    short inh;
    short idx;
};

#define R_VAR 1                  /* Global variable reference */
#define R_CALL 2

struct function {
#if defined(PROFILE_LPC)
    unsigned long long num_calls;	/* Number of times this function called */
    double time_spent;	/* cpu spent inside this function */
    double tot_time_spent; /* cpu spent inside this function and those called by it */
    double avg_time;    
    double avg_tot_time;    
    double avg_calls;
    double last_call_update;
#endif
    char *name;

    short hash_idx;
    unsigned short type_flags;	/* Return type of function. See below. */
				/* NAME_ . See above. */

    unsigned short offset;	/* Address of function,
				 * or inherit table index when inherited. */
    unsigned char num_local;	/* Number of local variables */
    char num_arg;	        /* Number of arguments needed.
				   -1 arguments means function not defined
				   in this object. Probably inherited */
    char first_default;
};				

struct function_hash {
    char *name;
    short next_hashed_function;
    short func_index;
};

struct variable {
    char *name;
    unsigned short type;	/* Type of variable. See below. TYPE_ */
};

struct inherit {
    struct program *prog;
    char *name;
    unsigned short variable_index_offset;
/* Only TYPE_MOD_PRIVATE, TYPE_MOD_STATIC, TYPE_MOD_NO_MASK, and
   TYPE_MOD_PUBLIC apply. */
    unsigned short type;    /* Type of inherit. See below. TYPE_ */
};


struct segment_desc 
{
    int ptr_offset;
    int swap_idx_offset;
    int size_offset;
    struct section_desc
    {
	int section;
	int ptr_offset;
	int num_offset;
	int ent_size;
    } *sections;
};
extern struct segment_desc segm_desc[];

#define A_HEADER		0
#define A_PROGRAM		1
#define A_FUNCTIONS		2
#define A_RODATA		3
#define A_VARIABLES		4
#define A_LINENUMBERS		5
#define A_INHERITS		6
#define A_ARGUMENT_TYPES	7
#define A_ARGUMENT_INDEX	8
#define A_INCLUDES           	9
#define A_RELOC                 10
#define A_FUNC_HASH		11
#define A_CFUN			12
#define NUMPAREAS             	13

#define A_CASE_NUMBERS          (NUMPAREAS + 0)
#define A_CASE_STRINGS          (NUMPAREAS + 1)
#define A_CASE_LABELS        	(NUMPAREAS + 2)
#define A_STRTAB                (NUMPAREAS + 3)
#define A_LABELS             	(NUMPAREAS + 4)
#define A_JUMPS             	(NUMPAREAS + 5)
#define NUMAREAS             	(NUMPAREAS + 6)
#define A_NUM                   NUMAREAS

#define S_HDR  0
#define S_EXEQ 1
#define S_DBG  2
#define S_NULL 3
#define S_NUM  4

struct lineno {
  unsigned short code;
  unsigned short file;
  unsigned int lineno;
};

  
struct program {
    char *program;			/* The binary instructions */
    char *name;				/* Name of file that defined prog */
    struct program *next_all, *prev_all; /* pointers in the list of all
					    programs. */
    struct lineno *line_numbers;        /* Line number information
					   This is not stored in memory
					   but swapped in when needed.
					 */
    struct function *functions;
    struct function_hash *func_hash;
    char *rodata;			/* All strings uses by the program */
    struct variable *variable_names;	/* All variables defined */
    struct inherit *inherit;		/* List of inherited prgms */
    struct cfun_desc *cfuns;
    /*
     * The types of function arguments are saved where 'argument_types'
     * points. It can be a variable number of arguments, so allocation
     * is done dynamically. To know where first argument is found for
     * function 'n' (number of function), use 'type_start[n]'.
     * These two arrays will only be allocated if '#pragma save_types' has
     * been specified. This #pragma should be specified in files that are
     * commonly used for inheritance. There are several lines of code
     * that depends on the type length (16 bits) of 'type_start' (sorry !).
     */
    unsigned short *argument_types;
#define INDEX_START_NONE		65535
    unsigned short *type_start;

    char *include_files;
    struct object *clones;
    
#if defined(PROFILE_LPC)
    double cpu;				/* The amount of cpu taken up */
    double cpu_avg;
    double last_avg_update;
#endif
    int ref;				/* Reference count */
    int swap_num;
    long num_clones;
    unsigned int time_of_ref;
    
#ifdef DEBUG
    int extra_ref;			/* Used to verify ref count */
#endif
    int total_size;			/* Sum of all data in this struct */
    int debug_size;
    int exec_size;

    int load_time;                      /* Time of loding of the program */
    int id_number;			/* used to associate information with
					  this prog block without needing to
					  increase the reference count     */
    int swap_lineno_index;	        /* Index in swapfile for lineno info */
    int mod_time; /* (simulate.c) last time of modification of the */
		  /* corrseponding file /lib_entry    */

    unsigned short dtor_index;          /* destructor */
    unsigned short ctor_index;          /* constructor */
    unsigned short debug_flags;
    
    /*
     * And now some general size information.
     */
    unsigned short program_size;	/* size of this instruction code */
    unsigned short num_functions;
    unsigned short rodata_size;
    unsigned short num_variables;
    unsigned short num_inherited;

    unsigned short sizeof_line_numbers;
    unsigned short sizeof_include_files;
    unsigned short sizeof_argument_types;
    char flags;                         /* some useful flags */
#define PRAGMA_NO_CLONE		1
#define PRAGMA_NO_INHERIT	2
#define PRAGMA_NO_SHADOW	4
#define PRAGMA_RESIDENT		8
};

extern struct program *current_prog;
extern int inh_offset;

/*
 * Types available. The number '0' is valid as any type. These types
 * are only used by the compiler, when type checks are enabled. Compare with
 * the run-time types, named T_ interpret.h.
 */

#define TYPE_UNKNOWN	0	/* This type must be casted */
#define TYPE_NUMBER	1
#define TYPE_STRING	2
#define TYPE_VOID	3
#define TYPE_OBJECT	4
#define TYPE_ANY	5	/* Will match any type */
#define TYPE_MAPPING	6
#define TYPE_FLOAT      7
#define TYPE_FUNCTION	8
#define TYPE_NONE	9

#define TYPE_LVALUE	0x10	/* or'ed in temporarily */
/*
 * These are or'ed in on top of the basic type.
 */
#define TYPE_MOD_STATIC		0x0100	/* Static function or variable */
#define TYPE_MOD_NO_MASK	0x0200	/* The nomask => not redefineable */
#define TYPE_MOD_POINTER	0x0400	/* Pointer to a basic type */
#define TYPE_MOD_PRIVATE	0x0800	/* Can't be inherited */
#define TYPE_MOD_PUBLIC		0x1000  /* Force inherit through private */
#define TYPE_MOD_VARARGS	0x2000	/* Used for type checking */
#define TYPE_MOD_TRUE_VARARGS   0x4000  /* The new true varargs */

#define TYPE_MOD_SECOND         0x0080  /* Muliple inheritance (only valid for inherit) */
/*
 * When an new object inherits from another, all function definitions
 * are copied, and all variable definitions.
 * Flags below can't explicitly declared. Flags that can be declared,
 * are found with TYPE_ above.
 *
 * When an object is compiled with type testing NAME_STRICT_TYPES, all
 * types are saved of the arguments for that function during compilation.
 * If the #pragma save_types is specified, then the types are saved even
 * after compilation, to be used when the object is inherited.
 *
 * Functions in a compiled program can only have the NAME_STRICT_TYPES flag.
 */
#define NAME_STRICT_TYPES   0x10 /* Compiled with type testing */
#define NAME_PROTOTYPE      0x20 /* Defined by a prototype only */
#define NAME_ABSTRACT       0x40 /* Function is implemented in C */

#define TYPE_MASK		(~(TYPE_MOD_STATIC | TYPE_MOD_NO_MASK |\
				   TYPE_MOD_PRIVATE | TYPE_MOD_PUBLIC |\
				   TYPE_MOD_VARARGS | TYPE_MOD_TRUE_VARARGS |\
				   NAME_ABSTRACT | NAME_STRICT_TYPES |\
				   NAME_PROTOTYPE))
#define TYPE_MOD_MASK         (TYPE_MOD_STATIC | TYPE_MOD_NO_MASK |\
                                TYPE_MOD_PRIVATE | TYPE_MOD_PUBLIC |\
				TYPE_MOD_VARARGS | TYPE_MOD_TRUE_VARARGS)
