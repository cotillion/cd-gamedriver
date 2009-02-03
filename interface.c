#include <stdio.h>
#include <string.h>
#include "lint.h"
#include "interface.h"

void init_cfuns(void);

extern struct interface efun;
extern struct interface stdobject;
extern struct interface gl_language;

extern struct interface auto_interface;

struct interface *(interface[]) = 
{
    &efun,
    &stdobject,
    &gl_language,
    (struct interface *)0,
};

void (*
get_C_fun_address(char *prog_name, char *name))(struct svalue *)
{
    int i, j;
    for(i = 0; interface[i]; i++)
	if (strcmp(interface[i]->program, prog_name) == 0)
	    for(j = 0; interface[i]->funcs[j]; j++)
		if (strcmp(interface[i]->funcs[j]->name, name) == 0)
		    return interface[i]->funcs[j]->address;
    
    return (void (*)(struct svalue *))0;
}

void
init_cfuns()
{
    int i,j;

    for(i = 0; interface[i]; i++)
	for(j = 0; interface[i]->vars[j]; j++)
	    interface[i]->vars[j]->num = j;
}
