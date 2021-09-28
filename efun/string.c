#include <string.h>
#include <ctype.h>

#include "../mstring.h"
#include "../interpret.h"

void
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

void
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

void
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

void
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

void
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