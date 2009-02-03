#include "config.h"
#include "exec.h"
#include "interpret.h"

#define VAR(i) (current_object->variables[\
		current_object->prog->inherit[inh_offset +\
		current_prog->cfuns[(i).num].inh].\
		variable_index_offset	+\
		current_prog->cfuns[(i).num].idx])

typedef struct var_info
{
    char *name;
    short num;
} var;

typedef struct func_info
{
    char *name;
    void (*address)(struct svalue *);
} func;

struct interface
{
    char *program;
    var  **vars;
    func **funcs;
};

extern struct interface *(interface[]);

