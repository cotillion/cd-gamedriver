#include <stdio.h>
#include <string.h>
#if defined(sun) || defined(__osf__)
#include <alloca.h>
#endif

#include "../lint.h"
#include "../interface.h"
#include "../object.h"
/* Define variables */
extern struct object *previous_ob;

/* Define functions */

#ifdef _SEQUENT_
extern void *alloca(unsigned);
#endif

char lower(char c)
{
    char   ret;

    ret = c;
    if (ret < 96)
        ret += 32;
    return ret;
}

static void
global_article(struct svalue *fp)
{
    char    *tmp;

    if (fp[0].type != T_STRING)
    {
        push_number(0);
	return;
    }
    tmp = alloca((strlen(fp[0].u.string) + 2));
    (void)strcpy(tmp, (char *)(fp[0].u.string));
    (void)strcat(tmp, " ");
    tmp[0] = (char)lower(tmp[0]);

    if (!strncmp(tmp, "the ", 4))
    {
        push_string("", STRING_CSTRING);
        return;
    }
    if (tmp[0] == 'a' || tmp[0] == 'e' || tmp[0] == 'i' ||
	tmp[0] == 'o' || tmp[0] == 'u')
    {
        push_string("an", STRING_CSTRING);
	return;
    }
    push_string("a", STRING_CSTRING);
    return;
}

static func func_article = 
{
    "article",
    global_article,
};

static void
global_plural_word(struct svalue *fp)
{
    char *tmp, *str, ultimate, penultimate;
    size_t sl;
    
    if (fp[0].type != T_STRING)
    {
	push_number(0);
	return;
    }
    str = fp[0].u.string;

    if (strcmp(str, "tooth") == 0)
    {
	push_string("teeth", STRING_CSTRING); return;
    }
    else if (strcmp(str, "foot") == 0)
    {
	push_string("feet", STRING_CSTRING); return;
    }
    else if (strcmp(str, "man") == 0)
    {
	push_string("men", STRING_CSTRING); return;
    }
    else if (strcmp(str, "woman") == 0)
    {
	push_string("women", STRING_CSTRING); return;
    }
    else if (strcmp(str, "child") == 0)
    {
	push_string("children", STRING_CSTRING); return;
    }
    else if (strcmp(str, "sheep") == 0)
    {
	push_string("sheep", STRING_CSTRING); return;
    }
    else if (strcmp(str, "key") == 0)
    {
	push_string("keys", STRING_CSTRING); return;
    }
    
    if ((sl = strlen(str)) < 3)
    {
	push_string(str, fp[0].string_type);
	return;
    }
    tmp = alloca((strlen((char *)str) + 10));
    (void)strncpy(tmp, str, sl);
    ultimate = str[sl - 1];
    penultimate = str[sl - 2];
    tmp[sl] = '\0';

    switch(ultimate)
    {
    case 's':
    case 'x':
    case 'h':
	(void)strcat(tmp, "es");
	break;
    case 'y':
	if (penultimate == 'a' || penultimate == 'e' || penultimate == 'o')
	    (void)strcat(tmp, "s");
	else
	{
	    tmp[sl - 1] = '\0';
	    (void)strcat(tmp, "ies");
	}
	break;
    case 'e':
	if (penultimate == 'f')
	{
	    tmp[sl - 2] = '\0';
	    (void)strcat(tmp, "ves");
	}
	else
	    (void)strcat(tmp, "s");
	break;
    case 'f':
	if (penultimate == 'f')
	{
	    tmp[sl - 2] = '\0';
	}
	tmp[sl - 1] = '\0';
	(void)strcat(tmp, "ves");
	break;
    default:
	(void)strcat(tmp, "s");
	break;
    }
    push_string(tmp, STRING_MSTRING);
    return;
}

static func func_plural_word = 
{
    "plural_word",
    global_plural_word,
};


static func *(funcs[]) =
{
    &func_article,
    &func_plural_word,
    0,
};
static var *(vars[]) =
{
   0,
};

struct interface gl_language = 
{
    "sys/global/language.c",
    vars,
    funcs,
};





