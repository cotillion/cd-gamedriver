#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "interpret.h"
#include "object.h"
#include "exec.h"
#include "lex.h"
#include "simulate.h"

void dump_smart_log (void);

struct object *simul_efun_ob = 0;

/*
 * If there is a simul_efun file, then take care of it and extract all
 * information we need.
 */
void 
get_simul_efun()
{
    struct object *ob;

#define SIMULFILE "secure/simul_efun.c"

    FILE *f;
    extern struct program *prog;
    extern char *current_file;
    extern int total_lines;
    extern int num_parse_error;
    extern char *inherit_file;
    extern char *current_loaded_file;
    extern void init_smart_log(void);

    f = fopen(SIMULFILE, "r");
    if (f == 0)
    {
	(void)fprintf(stderr, SIMULFILE " not found.\n");
	exit(1);
    }
#if 1
    init_smart_log();
    start_new_file(f);
    current_file = string_copy(SIMULFILE);	/* This one is freed below */
    current_loaded_file = SIMULFILE;
    compile_file();
    end_new_file();
    current_loaded_file = 0;
    total_lines = 0;
    (void)fclose(f);
    free(current_file);
    current_file = 0;
    dump_smart_log();
    if (inherit_file || num_parse_error > 0 || prog == 0) 
    {
	(void)fprintf(stderr, "Error in " SIMULFILE ".\n");
	return;
    }

    (void)fprintf(stderr,"%s loaded: %d functions\n", SIMULFILE,
	    prog->num_functions);
    if (prog->num_functions == 0)
	return;

    /*
	We make an object so that it can be updated.
     */
    ob = get_empty_object();
    ob->name = string_copy("secure/simul_efun");
    ob->prog = prog;
    ob->prog->flags |= PRAGMA_RESIDENT;
    
    simul_efun_ob = ob;
    add_ref(ob, "simul_efun");
    enter_object_hash(ob);	/* add name to fast object lookup table */
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
    current_object = 0;
#else
    simul_efun_ob = load_object(SIMULFILE, 1, 0);
#endif
    return;
}
