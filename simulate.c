#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <memory.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "stdio.h"
#include "interpret.h"
#include "object.h"
#include "mapping.h"
#include "sent.h"
#include "exec.h"
#include "comm.h"
#include "mudstat.h"
#include "incralloc.h"
#include "call_out.h"
#include "main.h"
#include "comm1.h"
#include "simulate.h"
#include "backend.h"

#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
#include "lex.h"
#endif

#include "inline_svalue.h"

extern int comp_flag;

char *inherit_file;

char *last_verb, *trig_verb;
extern int tot_alloc_dest_object, tot_removed_object;

extern int set_call (struct object *, struct sentence *, int),
    legal_path (char *);

void init_smart_log (void);
void dump_smart_log (void);
void remove_interactive (struct interactive *, int);
void add_action (struct closure *, struct svalue *, int);
void    ipc_remove(void),
    remove_all_players(void), end_new_file(void),
    destruct2(struct object *);
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void remove_all_objects(void);
static void free_all_sent(void);
void tmpclean(void);
#endif
char *dump_malloc_data(void);
void start_new_file (FILE *);

extern int d_flag, s_flag;

struct object *obj_list, *obj_list_destruct, *master_ob, *auto_ob;

struct object *current_object;      /* The object interpreting a function. */
struct object *command_giver;       /* Where the current command came from. */
struct object *current_interactive; /* The user who caused this execution */

int num_parse_error;		/* Number of errors in the parser. */

int total_num_prog_blocks = 0;
int total_prog_block_size = 0;
int total_program_size = 0;
int tot_alloc_variable_size = 0;

void shutdowngame(void);

struct object* find_object_no_create(char *);

int
find_status(struct program *prog, char *str, int not_type)
{
    int i, j;
    char *super_name = 0;
    char *sub_name;
    char *real_name;
    char *search;
    
    variable_index_found = variable_inherit_found = 255;
    real_name = strrchr(str, ':') + 1;
    sub_name = strchr(str, ':') + 2;
    
    if(!(real_name = (find_sstring((real_name == (char *)1) ? str : real_name))))
        return -1;
    if (sub_name == (char *)2)
    {
	for (i = 0; i < (int)prog->num_variables; i++)
	{
	    if (prog->variable_names[i].name == real_name &&
		!(prog->variable_names[i].type & not_type))
		{
		    variable_index_found = i;
		    variable_inherit_found = prog->num_inherited - 1;
		    variable_type_mod_found = prog->variable_names[i].type;
		    return prog->inherit[variable_inherit_found].variable_index_offset + i;
		}
	}
    }
    else if (sub_name - str > 2)
    {
	super_name = xalloc((size_t)(sub_name - str - 1));
	(void)memcpy(super_name, str, (size_t)(sub_name - str - 2));
	super_name[sub_name - str - 2] = 0;
	if (strcmp(super_name, "this") == 0)
	    return find_status(prog, sub_name, not_type);
    }
    else
	str = sub_name;
    
    for(j =  prog->num_inherited - 2; j >= 0 ;j -= prog->inherit[j].prog->num_inherited)
    {
	if (super_name &&
	    strcmp(super_name, prog->inherit[j].name) == 0)
	    search = sub_name;
	else
	    search = str;
	if (find_status(prog->inherit[j].prog, search, not_type) != -1)
	{
	    int type1 = prog->inherit[j].type;
	    if (variable_type_mod_found & TYPE_MOD_PUBLIC)
		type1 &= ~TYPE_MOD_PRIVATE;
	    if (variable_type_mod_found & TYPE_MOD_PRIVATE)
		type1 &= ~TYPE_MOD_PUBLIC;
	    variable_type_mod_found |= type1 & TYPE_MOD_MASK;
	    if (variable_type_mod_found & not_type)
		continue;
	    
	    variable_inherit_found += j -
		(prog->inherit[j].prog->num_inherited - 1);
	    return prog->inherit[variable_inherit_found].variable_index_offset + 
		variable_index_found;
	}
    }
    return -1;
}


void
swap_objects(struct object *ob1, struct object *ob2)
{
    extern void call_out_swap_objects(struct object *, struct object *);
    extern void stack_swap_objects(struct object *, struct object *);
    struct program  *tmp_prog;
    struct svalue *tmp_var;
    
    /* swap the objects */
    call_out_swap_objects(ob1, ob2);
    stack_swap_objects(ob1, ob2);
    
    tmp_var = ob1->variables;
    ob1->variables = ob2->variables;
    ob2->variables = tmp_var;
    
    tmp_prog = ob1->prog;
    ob1->prog = ob2->prog;
    ob2->prog = tmp_prog;

    /* Swap the O_CREATED bit */
    ob1->flags ^= ob2->flags & O_CREATED;
    ob2->flags ^= ob1->flags & O_CREATED;
    ob1->flags ^= ob2->flags & O_CREATED;
    
}


/*
 * Load an object definition from file. If the object wants to inherit
 * from an object that is not loaded, discard all, load the inherited object,
 * and reload again.
 *
 * In mudlib3.0 when loading inherited objects, their reset() is not called.
 *
 * Save the command_giver, because reset() in the new object might change
 * it.
 *
 * 
 */
char *current_loaded_file = 0;

struct object *
load_object(char *lname, int dont_reset, struct object *old_ob, int depth)
{
    FILE *f;
    extern int total_lines;

    struct object *ob, *save_command_giver = command_giver;
    extern struct program *prog;
    extern char *current_file;
    struct stat c_st;
    int name_length;
    char real_name[200], name[200];
    char new_ob_name[200];
    /* Truncate possible .c in the object name. */
    /* Remove leading '/' if any. */
    while(lname[0] == '/')
	lname++;
    (void)strncpy(name, lname, sizeof(name) - 1);
    name[sizeof name - 1] = '\0';
    name_length = strlen(name);
    if (name_length > sizeof name - 4)
	name_length = sizeof name - 4;
    name[name_length] = '\0';
    while (name[name_length-2] == '.' && name[name_length-1] == 'c') {
	name[name_length-2] = '\0';
	name_length -= 2;
    }
    if (old_ob)
    {
	struct object *invalid_ob;
	
	(void)strcpy(new_ob_name, name);
	(void)strcat(new_ob_name, "#0");
	if ( (invalid_ob = find_object2(new_ob_name)) != NULL )
	{
	    invalid_ob->prog->flags &= ~PRAGMA_RESIDENT;
	    destruct_object(invalid_ob);
	}
    }
    
    /*
     * First check that the c-file exists.
     */
    
    (void)strcpy(real_name, name);
    (void)strcat(real_name, ".c");

    if (depth > MAX_INHERIT)
    {
	(void)fprintf(stderr, "Inherit chain too long for %s\n", real_name);
	error("Inherit chain too long: %s\n", real_name);
	/* NOTREACHED */
    }

    if (stat(real_name, &c_st) == -1)
    {
	(void)fprintf(stderr, "File not found: %s\n", real_name);
	error("File not found: %s\n", real_name);
	/* NOTREACHED */
    }
    /*
     * Check if it is a legal name.
     */
    if (!legal_path(real_name))
    {
	(void)fprintf(stderr, "Illegal pathname: %s\n", real_name);
	error("Illegal path name: %s\n", real_name);
	/* NOTREACHED */
    }
    if (comp_flag)
	(void)fprintf(stderr, " compiling %s ...", real_name);
    f = fopen(real_name, "r");
    if (s_flag)
    {
	num_fileread++;
	num_compile++;
    }
	
    if (f == 0)
    {
	perror(real_name);
	error("Could not read the file: %s\n", real_name);
	/* NOTREACHED */
    }
    init_smart_log();
    start_new_file(f);
    current_file = string_copy(real_name);	/* This one is freed below */
    current_loaded_file = real_name;
    compile_file();
    end_new_file();
    current_loaded_file = 0;
    if (comp_flag)
    {
	if (inherit_file == 0)
	    (void)fprintf(stderr, " done\n");
	else
	    (void)fprintf(stderr, " suspended, compiling inherited file(s)\n");
    }
    update_compile_av(total_lines);
    total_lines = 0;
    (void)fclose(f);
    free(current_file);
    current_file = 0;
    dump_smart_log();
    /* Sorry, can not handle objects without programs yet. */
    if (inherit_file == 0 && (num_parse_error > 0 || prog == 0))
    {
	if (prog)
	    free_prog(prog);
	if (num_parse_error == 0 && prog == 0) {
	    error("No program in object !\n");
	    /* NOTREACHED */
	}
	if (!old_ob)
	    error("Error in loading object\n");
	return 0;
    }

    /*
     * This is an iterative process. If this object wants to inherit an
     * unloaded object, then discard current object, load the object to be
     * inherited and reload the current object again. The global variable
     * "inherit_file" will be set by lang.y to point to a file name.
     */
    if (inherit_file)
    {
	char *tmp = inherit_file;

	if (prog)
	{
	    free_prog(prog);
	    prog = 0;
	}
        {
	    char *t1 = tmp, *t2 = tmp + strlen(tmp);
	    while (*t1=='/') 
		t1++;
	    if (t2-t1 > 1) 
		if (*(--t2)=='c') 
		    if (*(--t2)=='.') 
			*t2='\000';

	    if (strcmp(t1, name) == 0)
	    {
		inherit_file = NULL;
		error("Illegal to inherit self.\n");
		/* NOTREACHED */
	    }
	}
	/* Extreme ugliness begins. inherit_file must be 0 when we call
	   load_object, but tmp is used as a parameter, so we can not free
	   the string until after the call
	*/
	inherit_file = NULL;

	(void)load_object(tmp, 1, 0, depth + 1);
	free(tmp);

	/* Extreme ugliness ends */

	ob = load_object(name, dont_reset, old_ob, depth);
	return ob;
    }
    ob = get_empty_object();
    if (!old_ob)
	ob->name = string_copy(name);	/* Shared string is no good here */
    else
    {
	ob->name = xalloc(strlen(name) + 3);
	(void)strcpy(ob->name, name);
	(void)strcat(ob->name, "#0");
    }
    ob->prog = prog;

    /*	
	add name to fast object lookup table 
    */
    enter_object_hash(ob);	

    {
	int num_var, i;
	extern int tot_alloc_variable_size;

	num_var = ob->prog->num_variables +
	    ob->prog->inherit[ob->prog->num_inherited - 1]
		.variable_index_offset + 1;
	if (ob->variables)
	    fatal("Object allready initialized!\n");
	
	ob->variables = (struct svalue *)
	    xalloc(num_var * sizeof(struct svalue));
	tot_alloc_variable_size += num_var * sizeof(struct svalue);
	for (i= 0; i<num_var; i++)
	    ob->variables[i] = const0;
	ob->variables++;
    }
    /* 
	We ought to add a flag here marking the object as unfinished
	so it can be removed if the following code causes an LPC error
     */

    if (master_ob)
    {
	int save_resident;

	push_object(current_object);
	push_object(ob);
	save_resident = ob->prog->flags & PRAGMA_RESIDENT;
	ob->prog->flags &= ~PRAGMA_RESIDENT;
	(void)apply_master_ob(M_LOADED_OBJECT, 2);
	if ((ob->flags & O_DESTRUCTED) == 0)
	    ob->prog->flags |= save_resident;
    }
    if (!(ob->flags & (O_CREATED | O_DESTRUCTED)) && !dont_reset)
	create_object(ob);
    
    if (ob->flags & O_DESTRUCTED)
	return 0;
    command_giver = save_command_giver;
    if (d_flag & DEBUG_LOAD && ob)
	debug_message("--%s loaded\n", ob->name);

    
    if (master_ob && (ob->prog->flags & PRAGMA_RESIDENT))
    {
	struct svalue *ret;
	push_object(ob);
	ret = apply_master_ob(M_VALID_RESIDENT, 1);
	if (ret && (ret->type != T_NUMBER || ret->u.number == 0))
	    ob->prog->flags &= ~PRAGMA_RESIDENT;
    }
	
    
    return ob;
}

char *
make_new_name(char *str)
{
    static int i = 1;
    char *p = xalloc(strlen(str) + 10);

    (void) sprintf(p, "%s#%d", str, i);
    i++;
    return p;
}
    

/*
 * Save the command_giver, because reset() in the new object might change
 * it.
 */
struct object *
clone_object(char *str1)
{
    struct object *ob, *new_ob;
    struct object *save_command_giver = command_giver;

    ob = find_object_no_create(str1);
    if (ob == 0 || ob->super || (ob->flags & O_CLONE) || ob->prog->flags & PRAGMA_NO_CLONE) {
	error("Cloning a bad object! (No Master, Master in environment or Master is clone)\n");
	/* NOTREACHED */
    }
    
    /* We do not want the heart beat to be running for unused copied objects */

    new_ob = get_empty_object();

    new_ob->name = make_new_name(ob->name);
    new_ob->flags |= O_CLONE;
    new_ob->prog = ob->prog;
    reference_prog (ob->prog, "clone_object");
    enter_object_hash(new_ob);	/* Add name to fast object lookup table */
    if (!current_object)
	fatal("clone_object() from no current_object !\n");
    
    
    {
	int num_var, i;
	extern int tot_alloc_variable_size;

	num_var = new_ob->prog->num_variables +
	    new_ob->prog->inherit[ob->prog->num_inherited - 1]
	    .variable_index_offset + 1;
	if (new_ob->variables)
	    fatal("Object allready initialized!\n");
	
	new_ob->variables = (struct svalue *)
	    xalloc(num_var * sizeof(struct svalue));
	tot_alloc_variable_size += num_var * sizeof(struct svalue);
	for (i= 0; i<num_var; i++)
	    new_ob->variables[i] = const0;
	new_ob->variables++;
    }
    if (master_ob)
    {
	push_object(current_object);
	push_object(new_ob);
	(void)apply_master_ob(M_CLONED_OBJECT, 2);
	if (new_ob->flags & O_DESTRUCTED)
	    return 0;
    }
    if (!(ob->flags & O_CREATED))
	create_object(new_ob);
    command_giver = save_command_giver;
    /* Never know what can happen ! :-( */
    if (new_ob->flags & O_DESTRUCTED)
	return 0;
    return new_ob;
}

struct object *
environment(struct svalue *arg)
{
    struct object *ob = current_object;

    if (arg && arg->type == T_OBJECT)
	ob = arg->u.ob;
    else if (arg && arg->type == T_STRING)
	ob = find_object2(arg->u.string);
    if (ob == 0 || ob->super == 0 || (ob->flags & O_DESTRUCTED))
	return 0;
    if (ob->flags & O_DESTRUCTED) {
	error("environment() off destructed object.\n");
	/* NOTREACHED */
    }
    return ob->super;
}

/*
 * Execute a command for an object. Copy the command into a
 * new buffer, because 'parse_command()' can modify the command.
 * If the object is not current object, static functions will not
 * be executed. This will prevent forcing players to do illegal things.
 *
 * Return cost of the command executed if success (> 0).
 * When failure, return 0.
 */
int 
command_for_object(char *str, struct object *ob)
{
    char buff[1000];
    extern int eval_cost;
    int save_eval_cost = eval_cost - 1000;

    if (strlen(str) > sizeof(buff) - 1) {
	error("Too long command.\n");
	/* NOTREACHED */
    }
    if (ob == 0)
	ob = current_object;
    else if (ob->flags & O_DESTRUCTED)
	return 0;
    (void)strncpy(buff, str, sizeof buff);
    buff[sizeof buff - 1] = '\0';
    if (parse_command(buff, ob))
	return eval_cost - save_eval_cost;
    else
	return 0;
}

/*
 * Search the inventory of the second argument 'ob' for instances
 * of the first.
 */
static struct object *object_present2 (char *, struct object *);

struct object *
object_present(struct svalue *v, struct svalue *where)
{
    int i;
    struct object *ret;

    if (v->type == T_OBJECT) 
	if (v->u.ob->flags & O_DESTRUCTED)
	    return 0;

    switch (where->type)
    {
    case T_OBJECT:
	if (where->u.ob->flags & O_DESTRUCTED)
	    break;

	if (v->type == T_OBJECT) 
	{
	    if (v->u.ob->super == where->u.ob)
		return v->u.ob;
	    else
		break;
	}
	else
	    return object_present2(v->u.string, where->u.ob->contains);
    case T_POINTER:
	if (v->type == T_OBJECT) 
	{
	    for (i = 0 ; i < where->u.vec->size ; i++)
	    {
		if (where->u.vec->item[i].type != T_OBJECT)
		    continue;
		if (v->u.ob->super == where->u.vec->item[i].u.ob)
		    return v->u.ob;
	    }
	}
	else
	{
	    for (i = 0 ; i < where->u.vec->size ; i++)
	    {
		if (where->u.vec->item[i].type != T_OBJECT)
		    continue;
		if ( (ret = 
		      object_present2(v->u.string, 
				      where->u.vec->item[i].u.ob->contains)) != NULL )
		    return ret;
	    }
	}
	break;
    default:
	error("Strange type %d in object_present.\n", where->type);
	/* NOTREACHED */
    }
    return 0;
}

static struct object *
object_present2(char *str, struct object *ob)
{
    struct svalue *ret;
    char *p;
    int count = 0, length;
    char *item;

    item = string_copy(str);
    length = strlen(item);
    p = item + length - 1;
    if (*p >= '0' && *p <= '9')
    {
	while(p > item && *p >= '0' && *p <= '9')
	    p--;
	if (p > item && *p == ' ')
	{
	    count = atoi(p+1) - 1;
	    *p = '\0';
	    length = p - item;	/* This is never used again ! */
	}
    }
    for (; ob; ob = ob->next_inv)
    {
	push_string(item, STRING_MSTRING);
	ret = apply("id", ob, 1, 1);
	if (ob->flags & O_DESTRUCTED)
	{
	    free(item);
	    return 0;
	}
	if (ret == 0 || (ret->type == T_NUMBER && ret->u.number == 0))
	    continue;
	if (count-- > 0)
	    continue;
	free(item);
	return ob;
    }
    free(item);
    return 0;
}

/*
 * Remove an object. It is first moved into the destruct list, and
 * not really destructed until later. (see destruct2()).
 * 
 * We will simply not allow the master object to be destructed, Ha!
 */
void
destruct_object(struct object *ob)
{
    struct gdexception exception_frame;
    extern struct object *vbfc_object;
    struct svalue *ret;
    struct object *sob, **pp;
    int si, i;

    if (ob == NULL || (ob->flags & O_DESTRUCTED))
	return;

    /*
     * If we are to destruct the master ob or the simul_efun ob
     * it must be reloadable.
     */
    if ( (!(ob->flags & O_CLONE) && (ob->prog->flags & PRAGMA_RESIDENT)) ||
	ob == auto_ob || ob == master_ob || ob == simul_efun_ob)
    {
	struct object *new_ob;
	new_ob = load_object(ob->name, 1, ob, 0);
	if (!new_ob) {
	    error("Can not compile the new %s, aborting destruction of the old.\n", ob->prog->name);
	    /* NOTREACHED */
	}

	/* Make sure that the master object and the simul_efun object
	 * stays loaded */
	if (ob == master_ob || ob == auto_ob ||
	    ob == simul_efun_ob)
	    ob->prog->flags |= PRAGMA_RESIDENT;

	swap_objects(ob, new_ob);
	if (ob == master_ob)
	{
            resolve_master_fkntab();
	}

        /* Keep a reference that will be freed in the event
           of an exception */
        push_object(new_ob);
        push_object(ob);
	if (new_ob && new_ob->flags & O_CREATED)
	    recreate_object(ob, new_ob);
        if (ob == master_ob) {
            load_parse_information();
        }
        pop_stack(); /* ob */
        if (new_ob->flags & O_DESTRUCTED) {
            pop_stack(); /* new_ob */
            return;
        }
        pop_stack(); /* new_ob */
	ob = new_ob;
	ob->prog->flags &= ~PRAGMA_RESIDENT;
    }

    
    push_object(ob);
    (void)apply_master_ob(M_DESTRUCTED_OBJECT, 1);
    if (ob->flags & O_DESTRUCTED) return;
    /*
     * If this is the first object being shadowed by another object, then
     * destruct the whole list of shadows.
     */
    if (ob->shadowed && !ob->shadowing)
    {
	struct svalue svp;
	struct object *ob2;

	svp.type = T_OBJECT;
	for (ob2 = ob->shadowed; ob2; )
	{
	    svp.u.ob = ob2;
	    ob2 = ob2->shadowed;
	    if (svp.u.ob->shadowed) {
	        free_object(svp.u.ob->shadowed, "destruct_object-5");
		svp.u.ob->shadowed = 0;
	    }
	    if (svp.u.ob->shadowing) {
	        free_object(svp.u.ob->shadowing, "destruct_object-6");
		svp.u.ob->shadowing = 0;
	    }
	    destruct_object(ob2);
	    if (ob->flags & O_DESTRUCTED) return;
	}
    }
    /*
     * The chain of shadows is a double linked list. Take care to update
     * it correctly.
     */
    if (ob->shadowing) {
	change_ref(ob->shadowing->shadowed, ob->shadowed, "destruct_object-1");
	ob->shadowing->shadowed = ob->shadowed;
    }

    if (ob->shadowed) {
	change_ref(ob->shadowed->shadowing, ob->shadowing, "destruct_object-2");
	ob->shadowed->shadowing = ob->shadowing;
    }
    if (ob->shadowing) {
        free_object(ob->shadowing, "destruct_object-3");
	ob->shadowing = 0;
    }
    if (ob->shadowed) {
        free_object(ob->shadowed, "destruct_object-4");
	ob->shadowed = 0;
    }

    if (d_flag & DEBUG_DESTRUCT)
	debug_message("Destruct object %s (ref %d)\n", ob->name, ob->ref);

    /*
     * There is nowhere to move the objects.
     */
    {
	struct svalue svp;
	svp.type = T_OBJECT;
	while(ob->contains)
	{
	    svp.u.ob = ob->contains;
	    push_object(ob->contains);
	    /* An error here will not leave destruct() in an inconsistent
	     * stage.
	     */
	    (void)apply_master_ob(M_DESTRUCT_ENVIRONMENT_OF,1);
	    if (ob->flags & O_DESTRUCTED) return;
	    if (svp.u.ob == ob->contains)
		destruct_object(ob->contains);
	    if (ob->flags & O_DESTRUCTED) return;
	}
    } 

    if (ob->interactive)
    {
	struct object *save=command_giver;

	command_giver=ob;
	if (ob->interactive->ed_buffer)
	{
	    extern void save_ed_buffer(void);

	    save_ed_buffer();
	}
	command_giver=save;
	remove_interactive(ob->interactive, 0);
	if (ob->flags & O_DESTRUCTED) return;
    }

    /*
     * Remove us out of this current room (if any).
     * Remove all sentences defined by this object from all objects here.
     */
    if (ob->super) {
	if (ob->super->flags & O_ENABLE_COMMANDS)
	    remove_sent(ob, ob->super);
	for (pp = &ob->super->contains; *pp; )
	{
	    if ((*pp)->flags & O_ENABLE_COMMANDS)
		remove_sent(ob, *pp);
	    if (*pp != ob)
		pp = &(*pp)->next_inv;
	    else
		*pp = (*pp)->next_inv;
	}
    }

    exception_frame.e_exception = NULL;
    exception_frame.e_catch = 1;

    /* Call destructors */
    if (ob->flags & O_CREATED)
    {
	for (i = ob->prog->num_inherited - 1; i >= 0; i--)
	    if (!(ob->prog->inherit[i].type & TYPE_MOD_SECOND) &&
		ob->prog->inherit[i].prog->dtor_index !=
		(unsigned short) -1)
	    {
		push_pop_error_context(1);

		/*
		 * The following assignments between i & si and
		 * ob & sob might seem unneccecary, but some
		 * compilers requires this to ensure correct
		 * operation with setjmp/longjmp.
		 */
		si = i;
		sob = ob;
		if (setjmp(exception_frame.e_context))
		{
		    i = si;
		    ob = sob;
		    push_pop_error_context(-1);
		    exception = exception_frame.e_exception;
		}
		else
		{
		    i = si;
		    ob = sob;
		    exception_frame.e_exception = exception;
		    exception = &exception_frame;
		    call_function(ob, i,
				  (unsigned int)ob->prog->inherit[i].prog->dtor_index, 0);
		    push_pop_error_context(-1);
		    exception = exception_frame.e_exception;

		    if (ob->flags & O_DESTRUCTED)
			return;
		}
	    }
    }

    remove_object_from_stack(ob);

    /*
     * Now remove us out of the list of all objects.
     * This must be done last, because an error in the above code would
     * halt execution.
     */

    remove_object_hash(ob);
    remove_task(ob->callout_task);
    delete_all_calls(ob);
    if (ob->living_name)
	remove_living_name(ob);
    ob->super = 0;
    ob->next_inv = 0;
    ob->contains = 0;
    ob->flags &= ~O_ENABLE_COMMANDS;
    ob->next_all = obj_list_destruct;
    ob->prev_all = 0;
    obj_list_destruct = ob;
    ob->flags |= O_DESTRUCTED;

    tot_alloc_dest_object++;
    if (ob == vbfc_object)
    {
	free_object(vbfc_object, "destruct_object");
	vbfc_object = 0;
	ret = apply_master_ob(M_GET_VBFC_OBJECT, 0);
	if (ret && ret->type == T_OBJECT)
	{
	    vbfc_object = ret->u.ob;
	    INCREF(vbfc_object->ref);
	}
    }
}
/*
 * This one is called when no program is executing from the main loop.
 */
void
destruct2(struct object *ob)
{
    struct sentence *s;
    int num_var;
    extern int tot_alloc_variable_size;

    if (d_flag & DEBUG_DESTRUCT) 
	debug_message("Destruct-2 object %s (ref %d)\n", ob->name, ob->ref);
    
    if (ob->interactive)
	remove_interactive(ob->interactive, 0);

    /*
     * We must deallocate variables here, not in 'free_object()'.
     * That is because one of the local variables may point to this object,
     * and deallocation of this pointer will also decrease the reference
     * count of this object. Otherwise, an object with a variable pointing
     * to itself, would never be freed.
     * Just in case the program in this object would continue to
     * execute, change string and object variables into the number 0.
     */
    {
	/*
	 * Deallocate variables in this object.
	 */
	int i;
	num_var = ob->prog->num_variables +
	    ob->prog->inherit[ob->prog->num_inherited - 1].
		variable_index_offset + 1;

	ob->variables--;
	for (i=0; i < num_var; i++) {
	    free_svalue(&ob->variables[i]);
	    ob->variables[i].type = T_NUMBER;
	    ob->variables[i].u.number = 0;
	}
	free((char *)(ob->variables));
	ob->variables = 0;
	tot_alloc_variable_size -= num_var * sizeof(struct svalue);
    }

    free_prog(ob->prog);
    ob->prog = 0;

    for (s = ob->sent; s;) 
    {
	struct sentence *next;
	next = s->next;
	free_sentence(s);
	s = next;
    }
    delete_all_calls(ob);
    free_object(ob, "destruct_object");
}

/*
 * This will enable an object to use commands normally only
 * accessible by interactive players.
 * Also check if the player is a wizard. Wizards must not affect the
 * value of the wizlist ranking.
 */

void
enable_commands(int num)
{
    if (current_object->flags & O_DESTRUCTED)
	return;
    if (d_flag & DEBUG_LIVING)
    {
	debug_message("Enable commands %s (ref %d)\n",
	    current_object->name, current_object->ref);
    }
    if (num)
    {
	current_object->flags |= O_ENABLE_COMMANDS;
	command_giver = current_object;
    }
    else
    {
	current_object->flags &= ~O_ENABLE_COMMANDS;
	if (command_giver == current_object)
	    command_giver = 0;
    }
}

/*
 * Set up a function in this object to be called with the next
 * user input string.
 */
int
input_to(struct closure *fun, int flag)
{
    struct sentence *s;

    if (!command_giver || command_giver->flags & O_DESTRUCTED)
	return 0;

    s = alloc_sentence();
    if (set_call(command_giver, s, flag)) {
	s->funct = fun;
	INCREF(fun->ref);
	return 1;
    } else {
	free_sentence(s);
	return 0;
    }
}

#define MAX_LINES 50

/*
 * This one is used by qsort in get_dir().
 */
int
pstrcmp(const void *p1, const void *p2)
{
    return strcmp(((const struct svalue *) p1)->u.string,
		  ((const struct svalue *) p2)->u.string);
}

/*
 * List files in directory. This function do same as standard list_files did,
 * but instead writing files right away to player this returns an array
 * containing those files. Actually most of code is copied from list_files()
 * function.
 * Differences with list_files:
 *
 *   - file_list("/w"); returns ({ "w" })
 *
 *   - file_list("/w/"); and file_list("/w/."); return contents of directory
 *     "/w"
 *
 *   - file_list("/");, file_list("."); and file_list("/."); return contents
 *     of directory "/"
 */
struct vector *
get_dir(char *path)
{
    struct vector *v;
    size_t count = 0;
    int i;
    DIR *dirp;
    int namelen, do_match = 0;
    struct dirent *de;
    struct stat st;
    char *temppath;
    char *p;
    char *regexp = 0;

    if (!path)
	return 0;

    path = check_valid_path(path, current_object, "get_dir", 0);

    if (path == 0)
	return 0;

    /*
     * We need to modify the returned path, and thus to make a
     * writeable copy.
     * The path "" needs 2 bytes to store ".\0".
     */
    temppath = (char *) alloca(strlen(path) + 2);
    if (strlen(path)<2) {
	temppath[0] = path[0] ? path[0] : '.';
	temppath[1] = '\000';
	p = temppath;
    } else {
	(void)strcpy(temppath, path);
	/*
	 * If path ends with '/' or "/." remove it
	 */
	if ((p = strrchr(temppath, '/')) == 0)
	    p = temppath;
	if ((p[0] == '/' && p[1] == '.' && p[2] == '\0') || 
	    (p[0] == '/' && p[1] == '\0'))
	    *p = '\0';
    }

    if (stat(temppath, &st) < 0)
    {
	if (*p == '\0')
	    return 0;
	regexp = (char *)alloca(strlen(p)+2);
	if (p != temppath)
	{
	    (void)strcpy(regexp, p + 1);
	    *p = '\0';
	}
	else
	{
	    (void)strcpy(regexp, p);
	    (void)strcpy(temppath, ".");
	}
	do_match = 1;
    }
    else if (*p != '\0' && strcmp(temppath, "."))
    {
	if (*p == '/' && *(p + 1) != '\0')
	    p++;
	v = allocate_array(1);
	v->item[0].type = T_STRING;
	v->item[0].string_type = STRING_MSTRING;
	v->item[0].u.string = make_mstring(p);
	return v;
    }

    if ((dirp = opendir(temppath)) == 0)
	return 0;

    /*
     *  Count files
     */
    for (de = readdir(dirp); de; de = readdir(dirp))
    {
	namelen = strlen(de->d_name);
	if (!do_match && (strcmp(de->d_name, ".") == 0 ||
			  strcmp(de->d_name, "..") == 0))
	    continue;
	if (do_match && !match_string(regexp, de->d_name))
	    continue;
	count++;
	if ( count >= MAX_ARRAY_SIZE)
	    break;
    }
    /*
     * Make array and put files on it.
     */
    v = allocate_array((int)count);
    if (count == 0)
    {
	/* This is the easy case */
	(void)closedir(dirp);
	return v;
    }
    rewinddir(dirp);
    for(i = 0, de = readdir(dirp); i < count; de = readdir(dirp))
    {
        namelen = strlen(de->d_name);
	if (!do_match && (strcmp(de->d_name, ".") == 0 ||
			  strcmp(de->d_name, "..") == 0))
	    continue;
	if (do_match && !match_string(regexp, de->d_name))
	    continue;
	de->d_name[namelen] = '\0';
	v->item[i].type = T_STRING;
	v->item[i].string_type = STRING_MSTRING;
	v->item[i].u.string = make_mstring(de->d_name);
	i++;
    }
    (void)closedir(dirp);
    /* Sort the names. */
    qsort((char *)v->item, count, sizeof v->item[0], pstrcmp);
    return v;
}

int 
tail(char *path)
{
    char buff[1000];
    FILE *f;
    struct stat st;
    off_t offset;
 
    path = check_valid_path(path, current_object, "tail", 0);

    if (path == 0)
        return 0;
    f = fopen(path, "r");
    if (s_flag)
	num_fileread++;

    if (f == 0)
	return 0;
    if (fstat(fileno(f), &st) == -1)
	fatal("Could not stat an open file.\n");
    offset = st.st_size - 54 * 20;
    if (offset < 0)
	offset = 0;
    if (fseek(f, (long) offset, 0) == -1)
	fatal("Could not seek.\n");
    /* Throw away the first incomplete line. */
    if (offset > 0)
	(void) fgets(buff, sizeof buff, f);
    while(fgets(buff, sizeof buff, f))
	(void)add_message("%s", buff);
    (void)fclose(f);
    return 1;
}


int 
remove_file(char *path)
{
    path = check_valid_path(path, current_object, "remove_file", 1);

    if (path == 0)
        return 0;
    if (unlink(path) == -1)
        return 0;
    return 1;
}

/* Find an object. If not loaded, load it !
 * The object may selfdestruct, which is the only case when 0 will be
 * returned.
 */

struct object *
find_object(char *str)
{
    struct object *ob;

    /* Remove leading '/' if any. */
    while(str[0] == '/')
	str++;
    ob = find_object2(str);
    if (ob)
	return ob;
    ob = load_object(str, 0, 0, 0);
    if (!ob || (ob->flags & O_DESTRUCTED))		/* *sigh* */
	return 0;
    return ob;
}

/* same as find_object, but don't call 'create()' if not loaded */
struct object *find_object_no_create(str)
    char *str;
{
    struct object *ob;

    /* Remove leading '/' if any. */
    while(str[0] == '/')
	str++;
    ob = find_object2(str);
    if (ob)
	return ob;
    ob = load_object(str, 1, 0, 0);
    if (!ob || (ob->flags & O_DESTRUCTED))		/* *sigh* */
	return 0;
    return ob;
}

/* Look for a loaded object. Return 0 if non found. */
struct object *
find_object2(char *str)
{
    register struct object *ob;
    register int length;

    /* Remove leading '/' if any. */
    while(str[0] == '/')
	str++;
    /* Truncate possible .c in the object name. */
    length = strlen(str);
    while (str[length-2] == '.' && str[length-1] == 'c') {
	/* A new writreable copy of the name is needed. */
	char *p;
	p = (char *)alloca(strlen(str)+1);
	(void)strcpy(p, str);
	str = p;
	str[length-2] = '\0';
	length -= 2;
    }
    if ( (ob = lookup_object_hash(str)) != NULL ) {
	return ob;
    }
    return 0;
}

#if 0

void 
apply_command(char *com)
{
    struct value *ret;

    if (command_giver == 0) {
	error("command_giver == 0 !\n");
	/* NOTREACHED */
    }
    ret = apply(com, command_giver->super, 0, 1);
    if (ret != 0)
    {
	(void)add_message("Result:");
	if (ret->type == T_STRING)
	    (void)add_message("%s\n", ret->u.string);
	if (ret->type == T_NUMBER)
	    (void)add_message("%lld\n", ret->u.number);
	if (ret->type == T_FLOAT)
	    (void)add_message("%.18Lg\n", ret->u.real);
    }
    else
	(void)add_message("Error apply_command: function %s not found.\n", com);
}
#endif /* 0 */

/*
 * Update actions
 *
 * Update an objects set of actions in all 'nearby' living objects.
 *
 * NOTE!
 *      It removes ALL actions defined by itself, including those added
 *	to itself. As actions added to onself is normally not done in init() 
 *	this will have to be taken care of specifically. This should be of
 *	little problem as living objects seldom export actions and would 
 *	therefore have little use of this efun.
 */
void 
update_actions(struct object *aob)
{
    struct object *pp, *ob, *next_ob;
    struct object *save_cmd = command_giver, *item = aob;
    struct svalue *ret;

    /*
     * Remove actions in objects environment and from objects in the
     * same environment. (parent and siblings)
     */
    if (item->super)
    {
	if (item->super->flags & O_ENABLE_COMMANDS)
	    remove_sent(item, item->super);

	for (pp = item->super->contains; pp; pp = pp->next_inv)
	{
	    if (pp != item && (pp->flags & O_ENABLE_COMMANDS))
		remove_sent(item, pp);
	}
    }

    /*
     * Remove actions from objects inside the object. (children)
     */
    for (pp = item->contains; pp; pp = pp->next_inv)
    {
	if (pp->flags & O_ENABLE_COMMANDS)
	    remove_sent(item, pp);
    }

    /*
     * Setup the new commands. The order is very important, as commands
     * in the room should override commands defined by the room.
     * Beware that init() may have moved 'item' !
     */

    /* Save where object is, so as to note a move in init() 
     */
    ob = item->super; 

    /*
     * Add actions in objects environment and to objects in the
     * same environment. (parent and siblings)
     */
    if (item->super)
    {
	if (item->super->flags & O_ENABLE_COMMANDS)
	{
	    command_giver = item->super;
#ifdef USE_ENCOUNTER_NOT_INIT
	    push_object(item);
	    ret = apply("encounter", item->super, 1, 1);
	    if (!ret)
		(void)apply("init", item, 0, 1);
#else
	    (void)apply("init", item, 0, 1);
#endif
	    if (ob != item->super || 
		(item->flags & O_DESTRUCTED) ||
		(ob->flags & O_DESTRUCTED))
	    {
		command_giver = save_cmd; /* marion */
		return;
	    }
	}

	for (pp = item->super->contains; pp;)
	{
	    next_ob = pp->next_inv;
	    if (pp != item && (pp->flags & O_ENABLE_COMMANDS))
	    {
		command_giver = pp;
#ifdef USE_ENCOUNTER_NOT_INIT
		push_object(item);
		ret = apply("encounter", pp, 1, 1);
		if (!ret)
		    (void)apply("init", item, 0, 1);
#else
		(void)apply("init", item, 0, 1);
#endif
		if (ob != item->super ||
		    (item->flags & O_DESTRUCTED) ||
		    (ob->flags & O_DESTRUCTED) ||
		    (next_ob->flags & O_DESTRUCTED))
		{
		    command_giver = save_cmd; /* marion */
		    return;
		}
	    }
	    pp = next_ob;
	}
    }

    /*
     * Add actions to objects inside the object. (children)
     */
    for (pp = item->contains; pp;)
    {
	next_ob = pp->next_inv;
	if (pp->flags & O_ENABLE_COMMANDS)
	{
	    command_giver = pp;
#ifdef USE_ENCOUNTER_NOT_INIT
	    push_object(item);
	    ret = apply("encounter", pp, 1, 1);
	    if (!ret)
		(void)apply("init", item, 0, 1);
#else
	    (void)apply("init", item, 0, 1);
#endif
	    if (ob != item->super ||
		(item->flags & O_DESTRUCTED) ||
		(ob->flags & O_DESTRUCTED) ||
		(next_ob->flags & O_DESTRUCTED))

	    {
		command_giver = save_cmd; /* marion */
		return;
	    }
	}
	pp = next_ob;
    }
}

/*
 * Transfer an object.
 * The object has to be taken from one inventory list and added to another.
 * The main work is to update all command definitions, depending on what is
 * living or not. Note that all objects in the same inventory are affected.
 *
 */
void 
move_object(struct object *dest)
{
    struct object **pp, *ob, *next_ob;
    struct object *save_cmd = command_giver, *item = current_object;
    struct svalue *ret;

    if (item->flags & O_DESTRUCTED)
	return;
    /* Recursive moves are not allowed. */
    for (ob = dest; ob; ob = ob->super)
	if (ob == item) {
	    error("Can't move object inside itself.\n");
	    /* NOTREACHED */
	}
    if (item->shadowing) {
	error("Can't move an object that is shadowing.\n");
	/* NOTREACHED */
    }

    /*
     * The master object approves objects. Approved and nonapproved
     * objects can not be moved into each other.
     */
    if (s_flag)
	num_move++;

    if (item->super)
    {
	int okey = 0;
		
	if (item->flags & O_ENABLE_COMMANDS) 
	    remove_sent(item->super, item);

	if (item->super->flags & O_ENABLE_COMMANDS)
	    remove_sent(item, item->super);

	for (pp = &item->super->contains; *pp;)
	{
	    if (*pp != item)
	    {
		if ((*pp)->flags & O_ENABLE_COMMANDS)
		    remove_sent(item, *pp);
		if (item->flags & O_ENABLE_COMMANDS)
		    remove_sent(*pp, item);
		pp = &(*pp)->next_inv;
		continue;
	    }
	    *pp = item->next_inv;
	    okey = 1;
	}
	if (!okey)
#if 0
	    fatal("Failed to find object %s in super list of %s.\n",
		  item->name, item->super->name);
#else
	    fatal("Failed to find object in super list\n");
#endif
    }
    item->next_inv = dest->contains;
    dest->contains = item;
    item->super = dest;
    /*
     * Setup the new commands. The order is very important, as commands
     * in the room should override commands defined by the room.
     * Beware that init() in the room may have moved 'item' !
     *
     * The call of init() should really be done by the object itself
     * It might be too slow, though :-(
     */
    if (item->flags & O_ENABLE_COMMANDS)
    {
	command_giver = item;
#ifdef USE_ENCOUNTER_NOT_INIT
	push_object(dest);
	ret = apply("encounter", item, 1, 1);
	if (!ret)
	    (void)apply("init", dest, 0, 1);
#else
	(void)apply("init", dest, 0, 1);
#endif
	if ((dest->flags & O_DESTRUCTED) || item->super != dest)
	{
	    command_giver = save_cmd; /* marion */
	    return;
	}
    }
    /*
     * Run init of the item once for every present player, and
     * for the environment (which can be a player).
     */
    for (ob = dest->contains; ob; ob=next_ob)
    {
	next_ob = ob->next_inv;
	if (ob == item)
	    continue;
	if (ob->flags & O_DESTRUCTED) {
	    error("An object was destructed at call of init()\n");
	    /*NOTREACHED */
	}
	if (ob->flags & O_ENABLE_COMMANDS)
	{
	    command_giver = ob;
#ifdef USE_ENCOUNTER_NOT_INIT
	    push_object(item);
	    ret = apply("encounter", ob, 1, 1);
	    if (!ret)
		(void)apply("init", item, 0, 1);
#else
	    (void)apply("init", item, 0, 1);
#endif
	    if (dest != item->super)
	    {
		command_giver = save_cmd; 
		return;
	    }
	}
	if (item->flags & O_DESTRUCTED) { /* marion */
	    error("The object to be moved was destructed at call of init()\n");
	    /* NOTREACHED */
	}
	if (item->flags & O_ENABLE_COMMANDS)
	{
	    command_giver = item;
#ifdef USE_ENCOUNTER_NOT_INIT
	    push_object(ob);
	    ret = apply("encounter", item, 1, 1);
	    if (!ret)
		(void)apply("init", ob, 0, 1);
#else
	    (void) apply("init", ob, 0, 1);
#endif
	    if (dest != item->super)
	    {
		command_giver = save_cmd;
		return;
	    }
	}
    }
    if (dest->flags & O_DESTRUCTED) { /* marion */
	error("The destination to move to was destructed at call of init()\n");
	/* NOTREACHED */
    }
    if (dest->flags & O_ENABLE_COMMANDS)
    {
	command_giver = dest;
#ifdef USE_ENCOUNTER_NOT_INIT
	push_object(item);
	ret = apply("encounter", dest, 1, 1);
	if (!ret)
	    (void)apply("init", item, 0, 1);
#else
	(void)apply("init", item, 0, 1);
#endif
    }
    command_giver = save_cmd;
}

#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
struct sentence *sent_free = NULL;
#endif
int tot_alloc_sentence;
int tot_current_alloc_sentence;

struct sentence *
alloc_sentence() 
{
    struct sentence *p;

    p = (struct sentence *) xalloc(sizeof *p);
    tot_alloc_sentence++;
    tot_current_alloc_sentence++;
    p->verb = NULL;
    p->funct = NULL;
    p->next = NULL;
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
    p->prev_all = NULL;
    p->next_all = sent_free;
    if (sent_free != NULL)
	sent_free->prev_all = p;
    sent_free = p;
#endif

    return p;
}

#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
static void
free_all_sent()
{
    struct sentence *p;

    for (; sent_free ; sent_free = p) {
	p = sent_free->next_all;
	free(sent_free);
	tot_current_alloc_sentence--;
	tot_alloc_sentence--;
    }
}
#endif

void 
free_sentence(struct sentence *p)
{
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
    if (p->prev_all)
	p->prev_all->next_all = p->next_all;
    if (p->next_all)
	p->next_all->prev_all = p->prev_all;
    if (p == sent_free)
	sent_free = p->next_all;
#endif
    if (p->funct)
	free_closure(p->funct);
    p->funct = NULL;
    if (p->verb)
	free_sstring(p->verb);
    p->verb = NULL;
    free((char *)p);
    tot_current_alloc_sentence--;
    tot_alloc_sentence--;
}

/*
 * Find the sentence for a command from the player.
 * Return success status.
 */
int 
player_parser(char *buff)
{
    struct sentence *s;
    size_t length;
    char *p;
    struct object *save_current_object = current_object;
    char verb_copy[100], *subst_buff = NULL;
    struct svalue *ret;
    struct object *cmd_giver = command_giver;
    struct object *theobj;

#ifdef DEBUG
    if (d_flag & DEBUG_COMMAND)
	debug_message("cmd [%s]: %s\n", cmd_giver->name, buff);
#endif
    /* 
     * Strip trailing spaces. 
     */
    for (p = buff + strlen(buff) - 1; p >= buff; p--)
    {
	if (*p != ' ')
	    break;
	*p = '\0';
    }
    /*
     * Strip leading spaces.
     */
    while(*buff == ' ')
	buff++;

    if (buff[0] == '\0')
	return 0;

    /*
     * Quicktyper hook.
     */
    push_string(buff, STRING_MSTRING);
    push_object(cmd_giver);
    ret = apply_master_ob(M_MODIFY_COMMAND, 2);
    
    if (ret && ret->type == T_STRING)
	subst_buff = string_copy(ret->u.string);
    else if (ret == NULL)
	subst_buff = string_copy(buff);
    
    if (subst_buff == NULL)
	return 1;

    p = strpbrk(subst_buff, " \t\v\f\r\n");
    if (p == 0)
	length = strlen(subst_buff);
    else
	length = p - subst_buff;
    if (!*subst_buff ||
	!cmd_giver || (cmd_giver->flags & O_DESTRUCTED))
	return 1;

    clear_notify();
    for (s = cmd_giver->sent; s; s = s->next) 
    {
	size_t len, copy_len;
	
	if (s->verb == 0) {
	    error("An 'action' did something, but returned 0 or had an undefined verb.\n");
	    /* NOTREACHED */
	}
	len = strlen(s->verb);
	if (s->short_verb)
	{
	    if (len && strncmp(s->verb, subst_buff, len) != 0)
		continue;
	    if (s->short_verb == V_NO_SPACE)
		length = len;
	    else {
#ifndef COMPAT_ADD_ACTIONS	/* Backwards compatibility */
	    	if (len && length > len)
		    continue;
#endif
		len = length;
	    }
	}
	else
	{
	    if (len != length)
		continue;
	    if (strncmp(subst_buff, s->verb, length))
		continue;
	}
	/*
	 * Now we have found a special sentence !
	 */
#ifdef DEBUG
	if (d_flag & DEBUG_COMMAND)
	    debug_message("Local command %s\n", getclosurename(s->funct));
#endif
	if (length >= sizeof verb_copy)
	    copy_len = sizeof verb_copy - 1;
	else
	    copy_len = length;
	(void)strncpy(verb_copy, subst_buff, copy_len);
	verb_copy[copy_len] = '\0';
	last_verb = verb_copy;
	trig_verb = s->verb;

	theobj = s->funct->funobj;

	/*
	 * If the function is static and not defined by current object,
	 * then it will fail. If this is called directly from player input,
	 * then we set current_object so that static functions are allowed.
	 * current_object is reset just after the call to apply().
	 */
	if (current_object == 0)
	    current_object = theobj;
	/*
	 * Remember the object, to update score.
	 */
	command_giver = cmd_giver;

	p = subst_buff + (s->short_verb == V_NO_SPACE ? len : length);
	while (*p != '\0' && isspace(*p))
	    p++;
#ifdef DEBUG
	if (d_flag & DEBUG_COMMAND)
	    debug_message("Verb is %s [%s], args \"%s\"\n", last_verb, trig_verb, p ? p : "[none]");
#endif
	if (*p != '\0') {
	    push_string(p, STRING_MSTRING);
	    (void)call_var(1, s->funct);
	}
	else
	{
	    (void)call_var(0, s->funct);
	}
	if (cmd_giver == 0) {
	    pop_stack();
	    if (subst_buff) 
		free(subst_buff);
	    return 1;
	}
	    
	current_object = save_current_object;
	last_verb = 0;
	trig_verb = 0;
	/* If we get fail from the call, it was wrong second argument. */
	if (sp->type == T_NUMBER && sp->u.number == 0) {
	    /* Has the sentences been lost? */
	    /* Does anyone know a better way of doing this? */
	    struct sentence *s0;
	    
	    pop_stack();
	    
	    for (s0 = cmd_giver->sent; s0 && s0 != s; s0 = s0->next)
		;
		
	    if (!s0) {
		s = 0;
		break;
	    }
	    continue;
	} else
	    pop_stack();
	break;
    }
    if (subst_buff)
        free(subst_buff);
    if (s == 0)
    {
	notify_no_command();
	return 0;
    }
    return 1;
}

/*
 * Associate a command with function in this object.
 * The optional second argument is the command name. 
 *
 * The optional third argument is a flag that will state that the verb should
 * only match against leading characters.
 *
 * The object must be near the command giver, so that we ensure that the
 * sentence is removed when the command giver leaves.
 *
 * If the call is from a shadow, make it look like it is really from
 * the shadowed object.
 */
void 
add_action(struct closure *func, struct svalue *cmd, int flag)
{
    struct sentence *p;
    struct object *ob;

    if (current_object->flags & O_DESTRUCTED)
	return;
    ob = current_object;
    if (command_giver == 0 || (command_giver->flags & O_DESTRUCTED))
	return;
    if (ob != command_giver && ob->super != command_giver &&
	ob->super != command_giver->super && ob != command_giver->super) {
	error("add_action from object that was not present.\n");
	/* NOTREACHED */
    }
    if (!legal_closure(func))
    {
	error("add_action to function in destructed object.\n");
	/* NOTREACHED */
    }
#ifdef DEBUG
    if (d_flag & DEBUG_ADD_ACTION)
	debug_message("--Add action %s\n", getclosurename(func));
#endif

/*printf("add_action %lx %s\n", func, show_closure(func));*/

    p = alloc_sentence();
    p->funct = func;
    INCREF(func->ref);
    p->next = command_giver->sent;
    p->short_verb = flag;
    if (cmd)
 	p->verb = make_sstring(cmd->u.string);
    else
	p->verb = 0;
    command_giver->sent = p;
}

/*
 * Remove all commands (sentences) defined by object 'ob' in object
 * 'player'
 */
void 
remove_sent(struct object *ob, struct object *player)
{
    struct sentence **s;

    for (s= &player->sent; *s;)
    {
	struct sentence *tmp;
	if ((*s)->funct->funobj == ob) {
	    if (d_flag & DEBUG_SENTENCE)
		debug_message("--Unlinking sentence %s\n", getclosurename((*s)->funct));
	    tmp = *s;
	    *s = tmp->next;
	    free_sentence(tmp);
	} else
	    s = &((*s)->next);
    }
    
}


static char debinf[8192];

char *
get_gamedriver_info(char *str)
{
    char tmp[1024];
    float proc;
    long tot;
    int res, verbose = 0;
    extern char *reserved_area;
    extern int tot_alloc_object, tot_alloc_object_size,
	num_arrays, total_array_size, num_mappings,
	total_mapping_size;
    extern int num_distinct_strings_shared, num_distinct_strings_malloced;
    extern long bytes_distinct_strings_shared, bytes_distinct_strings_malloced;
    extern long overhead_bytes_shared, overhead_bytes_malloced;
    extern int num_call, call_out_size;
    extern unsigned long long globcache_tries, globcache_hits;
    extern struct vector null_vector;
    extern unsigned long long cache_tries, cache_hits;
    extern int num_closures, total_closure_size;
    
    (void)strcpy(debinf,"");

    if (strcmp(str, "status") == 0 || strcmp(str, "status tables") == 0)
    {

	if (strcmp(str, "status tables") == 0)
	    verbose = 1;
	if (reserved_area)
	    res = RESERVED_SIZE;
	else
	    res = 0;
	if (!verbose)
	{
	    (void)sprintf(tmp,"Sentences:\t\t%12d %12d\n", tot_alloc_sentence,
		    tot_alloc_sentence * (int) sizeof (struct sentence));
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Objects:\t\t%12d %12d (%d dest, %d rmd)\n",
		    tot_alloc_object, tot_alloc_object_size,
		    tot_alloc_dest_object, tot_removed_object);
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Variables:\t\t\t     %12d\n",
		    tot_alloc_variable_size);
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Arrays:\t\t\t%12d %12d\n",
		    num_arrays,	total_array_size);
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Call_outs:\t\t%12d %12d\n", num_call,
			call_out_size);
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Mappings:\t\t%12d %12d\n",
		    num_mappings, total_mapping_size);
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Functions:\t\t%12d %12d\n",
		    num_closures, total_closure_size);
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Strings:\t\t%12d %12ld (%ld overhead)\n",
		    num_distinct_strings_shared + num_distinct_strings_malloced,
		    bytes_distinct_strings_shared + bytes_distinct_strings_malloced,
		    overhead_bytes_shared + overhead_bytes_malloced);
	    (void)strcat(debinf, tmp);
	    
	    (void)sprintf(tmp,"Prog blocks:\t\t%12d %12d\n",
		    total_num_prog_blocks, total_prog_block_size);
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Programs:\t\t\t     %12d\n", total_program_size);
	    (void)strcat(debinf, tmp);

	    (void)sprintf(tmp,"Memory reserved:\t\t     %12d\n", res);
	    (void)strcat(debinf, tmp);
	
	}
	if (verbose)
	{
#ifdef CACHE_STATS
	    extern long long call_cache_saves;
	    extern long long global_cache_saves;
	    extern long long searches_needed;
	    extern long long searches_done;
	    extern long long global_first_saves;
	    extern long long call_first_saves;
#endif

	    if (globcache_tries < 1)
		globcache_tries = 1;
	    (void)strcat(debinf, stat_living_objects());
	    (void)strcat(debinf, "\nFunction calls:\n--------------------------\n");
	    proc = 100.0 * (((double)cache_hits * 1.0) / cache_tries);
	    (void)sprintf(tmp,"Call cache,   Tries: %10lld Hits: %10lld Miss: %10Ld Rate: %3.2f%%\n",
		    cache_tries, cache_hits,
		    cache_tries - cache_hits, proc);
	    (void)strcat(debinf, tmp);
#ifdef CACHE_STATS
	    (void)sprintf(tmp, "searches saved       %10lld %3.2f%%\n",
		    call_first_saves,
		    (double)(100.0 * (((double)call_first_saves * 1.0) /
				   (searches_needed +
				    global_first_saves
				    + call_first_saves))));
	    (void)strcat(debinf, tmp);
#endif	    
	    
#ifdef GLOBAL_CACHE
	    proc = 100.0 * (((double)globcache_hits * 1.0) / globcache_tries);
	    (void)sprintf(tmp,"Global cache, Tries: %10lld Hits: %10lld Miss: %10lld Rate: %3.2f%%\n",
		    globcache_tries, globcache_hits,
		    globcache_tries -globcache_hits, proc);
	    (void)strcat(debinf, tmp);
#ifdef CACHE_STATS
	    (void)sprintf(tmp, "searches saved       %10lld %3.2f%%\n",
		    global_first_saves,
		    (double)(100.0 * (((double)global_first_saves * 1.0) /
				   (searches_needed +
				    global_first_saves
				    + call_first_saves))));
	    (void)strcat(debinf, tmp);
#endif
#endif

#ifdef CACHE_STATS
	    (void)strcat(debinf, "Secondary hits:\n");
	    (void)sprintf(tmp,"searches needed    : %10lld\n", searches_needed);
	    (void)strcat(debinf, tmp);
	    (void)sprintf(tmp,"call cache saves   : %10lld %3.2f%%\n",
		    call_cache_saves, (double)(100.0 * (((double)call_cache_saves * 1.0) /
						     searches_needed)));
	    (void)strcat(debinf, tmp);
	    (void)sprintf(tmp,"global cache saves : %10lld %3.2f%%\n",
		    global_cache_saves,
		    (double)(100.0 * (((double)global_cache_saves * 1.0) /
				   searches_needed)));
	    (void)strcat(debinf, tmp);
	    (void)sprintf(tmp,"searches done      : %10lld\n", searches_done);
	    (void)strcat(debinf, tmp);
	    (void)sprintf(tmp,"Total needed       : %10lld %3.2f%% done\n",
		    searches_needed + global_first_saves + call_first_saves,
		    (double)(100.0 * (((double)searches_done * 1.0) /
				   (searches_needed +
				    global_first_saves
				    + call_first_saves))));
	    (void)strcat(debinf, tmp);
#endif
	    add_string_status(debinf);

	    add_otable_status(debinf);

	    (void)strcat(debinf, "\nReferences:\n--------------------------------\n");
	    (void)sprintf(tmp, "Null vector: %d\n", null_vector.ref);
	    (void)strcat(debinf, tmp);
        }
    
	tot =		   total_prog_block_size +
	                   total_program_size +
	                   tot_alloc_sentence * sizeof(struct sentence) +
	                   total_array_size +
                           tot_alloc_variable_size +
		           call_out_size +
		           total_mapping_size +
			   bytes_distinct_strings_shared + overhead_bytes_shared +
			   bytes_distinct_strings_malloced + overhead_bytes_malloced +
			   tot_alloc_object_size + res;

	if (!verbose) 
	{
	    (void)strcat(debinf, "\t\t\t\t\t --------\n");
	    (void)sprintf(tmp,"Total:\t\t\t\t\t %8ld\n", tot);
	    (void)strcat(debinf, tmp);
	}
    }
    return make_mstring(debinf);
}

struct vector *
get_local_commands(struct object *ob)
{
    struct sentence *s;
    struct vector *ret;
    struct vector *ret2;
    int num;

    for (num = 0, s = ob->sent; s; s = s->next) 
	num++;

    ret2 = allocate_array(num);

/* XXX */
    for (num = 0, s = ob->sent; s; s = s->next, num++)
    {
	ret2->item[num].type = T_POINTER;
	ret2->item[num].u.vec = ret = allocate_array(4);

	ret->item[0].type = T_STRING;
	ret->item[0].string_type = STRING_SSTRING;
	ret->item[0].u.string = reference_sstring(s->verb);

	ret->item[1].type = T_NUMBER;
	ret->item[1].u.number = s->short_verb;

	if (s->funct->funobj) {
	    ret->item[2].type = T_OBJECT;
	    ret->item[2].u.ob = s->funct->funobj;
	    add_ref(ret->item[2].u.ob,"get_local_commands");
	}

	ret->item[3].type = T_STRING;
	ret->item[3].string_type = STRING_MSTRING;
	ret->item[3].u.string = make_mstring(getclosurename(s->funct));
    }
    return ret2;
}

/*VARARGS1*/
void
warning(char *format, ...)
{
    va_list  argptr;

    va_start(argptr, format);
    vfprintf(stderr, format, argptr);
    va_end(argptr);
    (void)fflush(stderr);
}

/*VARARGS1*/
void
fatal(char *fmt, ...)
{
    va_list argp;
    char buf[512];

    static int in_fatal = 0;
    /* Prevent double fatal. */
    if (in_fatal)
	abort();
    in_fatal = 1;

    va_start(argp, fmt);
    (void)vfprintf(stderr, fmt, argp);

    (void)fflush(stderr);
    if (current_object)
	(void) fprintf(stderr, "Current object was %s\n",
		      current_object->name);
    (void)vsprintf(buf, fmt, argp);
    /* LINTED: expression has null effect */
    va_end(argp);
    debug_message("%s", buf);

    if (current_object)
	debug_message("Current object was %s\n", current_object->name);
    debug_message("Dump of variables:\n");
    (void) dump_trace(1);
    abort();
}

int num_error = 0;

/*
 * Error() has been "fixed" so that users can catch and throw them.
 * To catch them nicely, we really have to provide decent error information.
 * Hence, all errors that are to be caught
 * (error_recovery_context_exists == 2) construct a string containing
 * the error message, which is returned as the
 * thrown value.  Users can throw their own error values however they choose.
 */

/*
 * This is here because throw constructs its own return value; we dont
 * want to replace it with the system's error string.
 */

void 
throw_error() 
{
    extern struct svalue catch_value;

    if (exception && exception->e_catch)
    {
	longjmp(exception->e_context, 1);
	fatal("Throw_error failed!\n");
    }
    if (catch_value.type == T_STRING)
	error("%s", catch_value.u.string);
    else
	error("Throw with no catch.\n");
    /* NOTREACHED */
}

static char emsg_buf[2000];

/*VARARGS1*/
void
error(char *fmt, ...)
{
    struct gdexception exception_frame;
    extern struct svalue catch_value;
    char *object_name;

    va_list argp;
    va_start(argp, fmt);
    (void)vsprintf(emsg_buf + 1, fmt, argp);
    /* LINTED: expression has null effect */
    va_end(argp);

    emsg_buf[0] = '*';	/* all system errors get a * at the start */
    if (exception != NULL && exception->e_catch)
    { /* user catches this error */
	free_svalue(&catch_value);
	catch_value.type = T_STRING;
	catch_value.string_type = STRING_MSTRING;	/* Always reallocate */
	catch_value.u.string = make_mstring(emsg_buf);
   	longjmp(exception->e_context, 1);
   	fatal("Catch() longjump failed\n");
    }
    if (num_error == 0)
    {
        time_t now = time(NULL);
	num_error = 1;
	debug_message("%s%s", ctime(&now), emsg_buf+1);

	object_name = dump_trace(0);
	(void)fflush(stdout);
	if (object_name) {
	    struct object *ob;
	    ob = find_object2(object_name);
	    if (!ob) {
		debug_message("error when executing program in destroyed object %s\n",
			      object_name);
	    }
	}
	/* 
	 * The stack must be brought in a usable state. After the
	 * call to reset_machine(), all arguments to error() are invalid,
	 * and may not be used any more. The reason is that some strings
	 * may have been on the stack machine stack, and has been deallocated.
	 */
	reset_machine ();
	push_string(emsg_buf, STRING_MSTRING);
	if (current_object)
	{
	    push_object(current_object);

	    if (current_prog)
		push_string(current_prog->name, STRING_MSTRING);
	    else
		push_string("<?? >", STRING_CSTRING);

	    push_string(get_srccode_position_if_any(), STRING_MSTRING);
	}
	else
	{
	    push_number(0);
	    push_string("<?? >", STRING_CSTRING);
	    push_string("", STRING_CSTRING);
	}

	exception_frame.e_exception = exception;
	exception_frame.e_catch = 1;
	if (setjmp(exception_frame.e_context))
	{
	    debug_message("Error while calling runtime_error()\n");
	    reset_machine();
	}
	else
	{	    
	    exception = &exception_frame;
	    (void)apply_master_ob(M_RUNTIME_ERROR, 4);
	}
	num_error = 0;
	exception = exception_frame.e_exception;
	
    }
    else
    {
	debug_message("Too many simultaneous errors.\n");
    }
    
    if (exception != NULL)
	longjmp(exception->e_context, 1);
    abort();
    /* NOTREACHED */
}

/*
 * Check that it is an legal path. No '..' are allowed.
 */
int 
legal_path(char *path)
{
    char *p;

    if (path == NULL || strchr(path, ' '))
	return 0;
    if (path[0] == '/')
        return 0;
    for(p = strchr(path, '.'); p; p = strchr(p+1, '.'))
    {
	if (p[1] == '.')
	    return 0;
    }
    return 1;
}

/*
 * There is an error in a specific file. Ask the game driver to log the
 * message somewhere.
 */
static struct error_msg {
    struct error_msg *next;
    char *file;
    char *msg;
} *smart_log_msg = NULL;

void
init_smart_log()
{
    struct error_msg *prev;

    while (smart_log_msg)
    {
	prev = smart_log_msg;
	smart_log_msg = smart_log_msg->next;
	free_mstring(prev->file);
	free_mstring(prev->msg);
	free((char *)prev);
    }
}

void
dump_smart_log()
{
    struct error_msg *prev;

    if (!master_ob)
    {
	init_smart_log();
	return;
    }

    while (smart_log_msg)
    {
	prev = smart_log_msg;
	smart_log_msg = smart_log_msg->next;
	push_mstring(prev->file);
	push_mstring(prev->msg);
	free((char *)prev);
	(void)apply_master_ob(M_LOG_ERROR, 2);
    }
}

void 
smart_log(char *error_file, int line, char *what)
{
    char buff[500];
    struct error_msg **last;

    for (last = &smart_log_msg; *last; last = &(*last)->next)
    if (error_file == 0)
	return;
    if (strlen(what) + strlen(error_file) > sizeof buff - 100)
	what = "...[too long error message]...";
    if (strlen(what) + strlen(error_file) > sizeof buff - 100)
	error_file = "...[too long filename]...";
    (void)sprintf(buff, "%s line %d:%s\n", error_file, line, what);
    *last = (struct error_msg *)xalloc(sizeof(struct error_msg));
    (*last)->next = NULL;
    (*last)->file = make_mstring(error_file);
    (*last)->msg = make_mstring(buff);
}

/*
 * Check that a path to a file is valid for read or write.
 * This is done by functions in the master object.
 * The path is always treated as an absolute path, and is returned without
 * a leading '/'.
 * If the path was '/', then '.' is returned.
 * The returned string may or may not be residing inside the argument 'path',
 * so don't deallocate arg 'path' until the returned result is used no longer.
 * Otherwise, the returned path is temporarily allocated by apply(), which
 * means it will be dealocated at next apply().
 */
char *
check_valid_path(char *path, struct object *ob,
                 char *call_fun, int writeflg)
{
    struct svalue *v;

    push_string(path, STRING_MSTRING);
    push_object(ob);
    push_string(call_fun, STRING_MSTRING);
    if (writeflg)
	v = apply_master_ob(M_VALID_WRITE, 3);
    else
	v = apply_master_ob(M_VALID_READ, 3);
    if (v && (v->type != T_NUMBER || v->u.number == 0))
	return 0;
    if (path[0] == '/')
	path++;
    if (path[0] == '\0')
	path = ".";
    if (legal_path(path))
	return path;
    return 0;
}

/*
 * This one is called from HUP.
 */
int game_is_being_shut_down;

/* ARGSUSED */
void 
startshutdowngame(int arg) 
{
    game_is_being_shut_down = 1;
}

/*
 * This one is called from the command "shutdown".
 * We don't call it directly from HUP, because it is dangerous when being
 * in an interrupt.
 */
void 
shutdowngame() 
{
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
    extern char *reserved_area, *inherit_file;
    struct lpc_predef_s *lp;
    char *p;
#endif

    remove_all_players();
    ipc_remove();
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
    if (reserved_area) {
	free(reserved_area);
	reserved_area = NULL;
    }
    free_parse_information();
    remove_all_objects();
    while ((lp = lpc_predefs) != NULL) {
	lpc_predefs = lpc_predefs->next;
	if (lp->flag)
	    free(lp->flag);
	free(lp);
    }
    free_inc_list();
    remove_destructed_objects();
    free_all_sent();
    tmpclean();
    (void)strcpy(debinf, p = get_gamedriver_info("status"));
    free_mstring(p);
    add_string_status(debinf + strlen(debinf));
    fputs(debinf, stderr);
    remove_string_hash();
    clear_otable();
    clear_ip_table();
    if (inherit_file)
	free(inherit_file);
    inherit_file = NULL;
    (void)apply(NULL, NULL, 0, 0);
    fputc('\n', stderr);
    fputs(dump_malloc_data(), stderr);
#endif
#ifdef OPCPROF
    opcdump();
#endif
}

int 
match_string(char *match, char *str)
{
    int i;

 again:
    if (*str == '\0' && *match == '\0')
	return 1;
    switch(*match)
    {
    case '?':
	if (*str == '\0')
	    return 0;
	str++;
	match++;
	goto again;
    case '*':
	match++;
	if (*match == '\0')
	    return 1;
	for (i=0; str[i] != '\0'; i++)
	    if (match_string(match, str+i))
		return 1;
	return 0;
    case '\0':
	return 0;
    case '\\':
	match++;
	if (*match == '\0')
	    return 0;
	/* FALLTHROUGH */
    default:
	if (*match == *str) {
	    match++;
	    str++;
	    goto again;
	}
	return 0;
    }
}

/*
 * Credits for some of the code below goes to Free Software Foundation
 * Copyright (C) 1990 Free Software Foundation, Inc.
 * See the GNU General Public License for more details.
 */
#ifndef S_ISDIR
#define	S_ISDIR(m)	(((m)&S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISREG
#define	S_ISREG(m)	(((m)&S_IFMT) == S_IFREG)
#endif

int
isdir(char *path)
{
  struct stat stats;

  return stat (path, &stats) == 0 && S_ISDIR (stats.st_mode);
}

void
strip_trailing_slashes(char *path)
{
    int last;
    
    last = strlen (path) - 1;
    while (last > 0 && path[last] == '/')
	path[last--] = '\0';
}

struct stat to_stats, from_stats;

int
copy (char *from, char *to)
{
    char buf[1024 * 8];
    int len;			/* Number of bytes read into buf. */
    int ifd;
    int ofd;
  
    if (!S_ISREG (from_stats.st_mode))
    {
        error("cannot move `%s' across filesystems: Not a regular file\n", from);
	/* NOTREACHED */
    }
  
    if (unlink (to) && errno != ENOENT)
    {
	error("cannot remove `%s'\n", to);
	/* NOTREACHED */
    }

    ifd = open (from, O_RDONLY, 0);
    if (ifd < 0)
    {
	error ("%s: open failed\n", from);
	/* NOTREACHED */
    }
    ofd = open (to, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (ofd < 0)
    {
	error ("%s: open failed\n", to);
	(void)close (ifd);
	return 1;
    }
    if (fchmod (ofd, (mode_t)(from_stats.st_mode & 0777)))
    {
	error ("%s: fchmod failed\n", to);
	(void)close (ifd);
	(void)close (ofd);
	(void)unlink (to);
	return 1;
    }
  
    while ((len = read (ifd, buf, sizeof (buf))) > 0)
    {
	int wrote = 0;
	char *bp = buf;
	
	do
	{
	    wrote = write (ofd, bp, (size_t)len);
	    if (wrote < 0)
	    {
		error ("%s: write failed\n", to);
		(void)close (ifd);
		(void)close (ofd);
		(void)unlink (to);
		return 1;
	    }
	    bp += wrote;
	    len -= wrote;
	} while (len > 0);
    }
    if (len < 0)
    {
	error ("%s: read failed\n", from);
	(void)close (ifd);
	(void)close (ofd);
	(void)unlink (to);
	return 1;
    }

    if (close (ifd) < 0)
    {
	error ("%s: close failed", from);
	(void)close (ofd);
	return 1;
    }
    if (close (ofd) < 0)
    {
	error ("%s: close failed", to);
	return 1;
    }
  
#ifdef FCHMOD_MISSING
    if (chmod (to, from_stats.st_mode & 0777))
    {
	error ("%s: chmod failed\n", to);
	return 1;
    }
#endif

    return 0;
}

/* Move FROM onto TO.  Handles cross-filesystem moves.
   If TO is a directory, FROM must be also.
   Return 0 if successful, 1 if an error occurred.  */

int
do_move (char *from, char *to)
{
    if (lstat (from, &from_stats) != 0)
    {
	error ("%s: lstat failed\n", from);
	return 0;
    }

    if (lstat (to, &to_stats) == 0)
    {
	if (from_stats.st_dev == to_stats.st_dev
	    && from_stats.st_ino == to_stats.st_ino)
	{
	    error ("`%s' and `%s' are the same file", from, to);
	    return 0;
	}

	if (S_ISDIR (to_stats.st_mode))
	{
	    error ("%s: cannot overwrite directory", to);
	    return 0;
	}

    }
    else if (errno != ENOENT)
    {
	error ("%s: unknown error\n", to);
	return 0;
    }
    if (rename (from, to) == 0)
    {
	return 1;
    }

    if (errno != EXDEV)
    {
	error ("cannot move `%s' to `%s'", from, to);
	return 0;
    }

  /* rename failed on cross-filesystem link.  Copy the file instead. */

    if (copy (from, to))
	return 0;
  
    if (unlink (from))
    {
	error ("cannot remove `%s'", from);
	return 0;
    }

    return 1;
}
    
/*
 * do_rename is used by the efun rename. It is basically a combination
 * of the unix system call rename and the unix command mv. Please shoot
 * the people at ATT who made Sys V.
 *
 * WARNING!  (Thanks to Sul@Genesis, pmidden@tolkien.helios.nd.edu)
 *
 *   If you have a typical 'lpmud ftpdaemon' that allow wizards to ftp
 *   to their homedirs and if they are allowed to move subdirs from
 *   under their wizdirs to for example /open then they can use ftp to
 *   access the entire host machine.
 *
 *   It is therefore important to disallow these directory moves in
 *   your /secure/master.c
 */

int
do_rename(char * fr, char *t)
{
    char *from, *to;
    
    from = check_valid_path(fr, current_object, "do_rename_from", 1);
    if(!from)
	return 0;
    to = check_valid_path(t, current_object, "do_rename_to", 1);
    if(!to)
	return 0;
    if(*to == '\0' && strcmp(t, "/") == 0)
    {
	to = (char *)alloca(3);
	(void)strcpy(to, "./");
    }
    strip_trailing_slashes (from);
    if (isdir (to))
    {
	/* Target is a directory; build full target filename. */
	char *cp;
	char *newto;
	
	cp = strrchr (from, '/');
	if (cp)
	    cp++;
	else
	    cp = from;
	
	newto = (char *) alloca (strlen (to) + 1 + strlen (cp) + 1);
	(void)sprintf (newto, "%s/%s", to, cp);
	return do_move (from, newto);
    }
    else
	return do_move (from, to);
}
