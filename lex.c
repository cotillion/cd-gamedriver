#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

#include "config.h"
#include "instrs.h"
#include "lint.h"
#include "lang.h"
#include "string.h"
#include "interpret.h"
#include "exec.h"
#include "lex.h"
#include "mstring.h"
#include "mudstat.h"
#include "simulate.h"
#include "efun_table.h"
#include "backend.h"
#include "hash.h"

#define isalunum(c) (isalnum(c) || (c) == '_')
#define NELEM(a) (sizeof (a) / sizeof((a)[0]))

#define WARNING 0
#define ERROR   1

int current_line;
int total_lines;    /* Used to compute average compiled lines/s */
char *current_file;
int pragma_strict_types;    /* Force usage of strict types. */
int pragma_no_clone;
int pragma_no_inherit;
int pragma_no_shadow;
int pragma_resident;

extern void smart_log (char *, int, char *);
struct lpc_predef_s *lpc_predefs=NULL;
static int number (long long), real (double), ident (char *), string (char *);
static void handle_define (char *);
static void free_defines (void), add_define (char *, int, char *);
static int expand_define (void);
static void add_input (char *);
static void myungetc (int);
static int lookup_resword (char *);
static int cond_get_exp (int);
static int exgetc (void);
static void refill (void);
static int cmygetc (void);
static int yylex1 (void);
static void skip_comment (void);
static void skip_comment2 (void);
static INLINE int mygetc (void);

static FILE *yyin;
static int lex_fatal;
static char **inc_list;
static int inc_list_size;


#define EXPANDMAX 25000
static int nexpands;

extern int s_flag;

#ifndef tolower
extern int tolower (int);
#endif

void yyerror(char *), error(char *, ...);
int yylex (void);

#define MAXLINE 1024
static char yytext[MAXLINE];
static int slast, lastchar;
static int num_incfiles, current_incfile, incdepth;

struct defn {
    struct defn *next;
    char *name;
    int undef;
    char *exps;
    int nargs;
};
struct defn *lookup_define(char *);

static struct ifstate {
    struct ifstate *next;
    int state;
} *iftop = 0;
#define EXPECT_ELSE 1
#define EXPECT_ENDIF 2

static struct incstate {
    struct incstate *next;
    FILE *yyin;
    int incfnum;
    int line;
    char *file;
    int slast, lastchar;
    int pragma_strict_types;
    int nbuf;
    char *outp;
} *inctop = 0;

/* DEFMAX must be even. We divide it by 2 in mygetc(). */
#define DEFMAX 20000
static char defbuf[DEFMAX];
static int nbuf;
static char *outp;

static struct {
    int token;
    int line;
    YYSTYPE lval;
} keep1, keep2, keep3, keep4;

static void 
calculate_include_path(char *name, char *dest)
{
    char *current;

    if ( (current = strrchr(dest, '/')) == NULL)   /* strip filename */
    {
	/* current_file is in the root directory */
	current = dest;
    }
    *current = '\0';

    while (*name == '/')
    {
	name++;
	current = dest;
	*current = '\0';  /* absolute path */
    }

    while (*name)
    {
	if (strncmp(name, "../", 3) == 0)
	{
	    if (*dest == '\0') /* including from above mudlib is NOT allowed */
		break;

	    /* Remove previous path element */
	    while (current > dest)
	    {
		*current-- = '\0';
		if (*current == '/')
		    break;
	    }
	    if (current == dest)
	    {
		*current = '\0';
	    }

	    name += 3;   /* skip "../" */
	}
	else if (strncmp(name, "./", 2) == 0)
	{
	    name += 2;
	}
	else
	{ /* append first component to dest */
	    if (*dest)
		*current++ = '/';    /* only if dest is not empty !! */
	    while (*name != '\0' && *name != '/')
		*current++ = *name++;
	    if (*name == '/')
		name++;
	    else
		*current = '\0'; /* Last element */
	}
    }
}

static INLINE int
mygetc(void)
{
    if (!nbuf)
    {
        char  buffer[(DEFMAX / 2) + 1];

        if (feof(yyin))
        {
            return EOF;
        } 

        nbuf  = (int)fread(buffer, sizeof(char), (DEFMAX / 2), yyin);
        outp -= nbuf;
        memcpy(outp, buffer, nbuf);
    }
 
    lastchar = slast;
    slast = *outp;
    nbuf--;
    outp++;
 
    return slast;
}

static INLINE int
gobble(int c)
{
    int d;
    d = mygetc();
    if (c == d)
	return 1;
    *--outp = d;
    nbuf++;
    return 0;
}

static void
lexerror(char *s)
{
    yyerror(s);
    lex_fatal++;
}

static void
lexwarning(char *str)
{
    (void)fprintf(stderr, "%s: Warning: %s line %d\n", current_file, str,
          current_line);
    (void)fflush(stderr);
    smart_log(current_file, current_line, str);
}

static int
skip_to(char *token, char *atoken)
{
    char b[20], *p;
    int c;
    int nest;

    for (nest = 0;;)
    {
    c = mygetc();
    if (c == '#')
    {
        do
        {
	    c = mygetc();
        } while (isspace(c));
        for (p = b; c != '\n' && c != EOF; )
        {
	    if (p < b+sizeof b-1)
		*p++ = c;
	    c = mygetc();
        }
        *p++ = 0;
        for (p = b; *p && !isspace(*p); p++)
	    ;
        *p = 0;
	/*(void)fprintf(stderr, "skip checks %s\n", b);*/
        if (strcmp(b, "if") == 0 || strcmp(b, "ifdef") == 0 ||
	    strcmp(b, "ifndef") == 0)
	{
	    nest++;
        }
        else if (nest > 0)
        {
	    if (strcmp(b, "endif") == 0)
		nest--;
        }
        else
        {
	    if (strcmp(b, token) == 0)
		return 1;
	    else if (atoken && strcmp(b, atoken) == 0)
		return 0;
        }
    }
    else 
    {
        /*(void)fprintf(stderr, "skipping (%d) %c", c, c);*/
	while (c != '\n' && c != EOF) 
        {
	    c = mygetc();
	    /*(void)fprintf(stderr, "%c", c);*/
        } 
        if (c == EOF)
        {
	    lexerror("Unexpected end of file while skipping");
	    return 1;
        }
    }
    store_line_number_info(current_incfile, current_line);
    current_line++;
    total_lines++;
    }
}

static void
handle_cond(int c)
{
    struct ifstate *p;

    /*(void)fprintf(stderr, "cond %d\n", c);*/
    if (c || skip_to("else", "endif")) 
    {
	p = (struct ifstate *)xalloc(sizeof(struct ifstate));
	p->next = iftop;
	iftop = p;
	p->state = c ? EXPECT_ELSE : EXPECT_ENDIF;
    }
    if (!c) 
    {
	store_line_number_info(current_incfile, current_line);
	current_line++;
	total_lines++;
    }
}

/* Make sure the path does not have any ".." elements. */
char *
check_valid_compile_path(char *path, char *file_name, char *calling_function)
{
#if 0
    struct svalue *ret;
#endif
    char *p = path;
    
    while (*p)
    {
	if (p[0] == '.' && p[1] == '.')
	    return NULL;
	p++;
    }
#if 0
    push_string(path, STRING_MSTRING);
    push_string(file_name, STRING_MSTRING);
    push_string(calling_function, STRING_MSTRING);
    ret = apply_master_ob(M_VALID_COMPILE_PATH, 3);
    if (ret)
	path = tmpstring_copy(ret->u.string);
#endif
    return path;
}

/* Try to load the file who's full path is specified in buf */
static INLINE FILE *
inc_try(char *buf)
{
    struct incstate *inc;
    char errbuf[1024];
    char *new_name;
    FILE *f;

    extern void remember_include(char *);
    extern char *current_loaded_file;

    new_name = check_valid_compile_path(buf, current_loaded_file, "include");
    if (!new_name)
    {
	lexerror("Invalid include.");
	return NULL;
    }
    if (new_name && (f = fopen(new_name, "r")) != NULL)
    {
#ifdef WARN_INCLUDES
	for (inc = inctop ; inc ; inc = inc->next) 
	    if (strcmp(inc->file, new_name) == 0)
	    {
		(void)snprintf(errbuf, sizeof(errbuf), "File /%s already included,", buf);
		lexwarning(errbuf);
	    }
#endif
	if (s_flag)
	    num_fileread++;
	remember_include(new_name);
	return f;
    }
    return NULL;
}

/* Find and open a file that has been specified in a "#include <file>" */
static INLINE FILE *
inc_open(char *buf, char *name)
{
    int i;
    FILE *f;

    if (incdepth >= MAX_INCLUDE)
    {
	lexerror("To deep recursion of includes.");
	return NULL;
    }
    (void)strcpy(buf, current_file);
    calculate_include_path(name, buf);
    if ((f = inc_try(buf)) != NULL)
	return f;
    /*
     * Search all include dirs specified.
     */
    for (i = 0; i < inc_list_size; i++) 
    {
        (void)sprintf(buf, "%s%s", inc_list[i], name);
	if ((f = inc_try(buf)) != NULL)
	    return f;
    }
    return NULL;
}

int
handle_include(char *name, int ignore_errors)
{
    char *p;
    char buf[1024];
    FILE *f;
    struct incstate *is;
    int delim;

    if (*name != '"' && *name != '<')
    {
	struct defn *d;
	if ((d = lookup_define(name)) && d->nargs == -1)
	{
	    char *q;
	    q = d->exps;
	    while (isspace(*q))
		q++;
	    return handle_include(q, ignore_errors);
	}
	else
	{
            if (!ignore_errors)
	        lexerror("Missing leading \" or < in #include");
            return 0;
	}
    }
    delim = *name++ == '"' ? '"' : '>';
    for (p = name; *p && *p != delim; p++)
	;
    if (!*p)
    {
        if (!ignore_errors)
	    lexerror("Missing trailing \" or > in #include");
	return 0;
    }
    if (strlen(name) > sizeof(buf) - 100)
    { 
        if (!ignore_errors)
	    lexerror("Include name too long.");
	return 0;
    }
    *p = 0;
    
    if ((f = inc_open(buf, name)) == NULL)
    {
        if (!ignore_errors) {
	    (void)sprintf(buf, "Cannot #include %s\n", name);
	    lexerror(buf);
        }
        return 0;
    }

    is = (struct incstate *)xalloc(sizeof(struct incstate));
    is->yyin = yyin;
    is->line = current_line;
    is->file = current_file;
    is->incfnum = current_incfile;
    is->slast = slast;
    is->lastchar = lastchar;
    is->next = inctop;
    is->pragma_strict_types = pragma_strict_types;

    if (nbuf)
    {
        memcpy(is->outp = (char *)xalloc(nbuf + 1), outp, nbuf);
        is->nbuf = nbuf;
        nbuf = 0;
        outp = defbuf + DEFMAX;
    }
    else
    {
        is->nbuf = 0;
        is->outp = NULL;
    }

    pragma_strict_types = 0;
    inctop = is;
    current_line = 1;
    current_file = xalloc(strlen(buf)+1);
    current_incfile = ++num_incfiles;
    (void)strcpy(current_file, buf);
    slast = lastchar = '\n';
    yyin = f;
    incdepth++;
    return 1;
}

static void
handle_exception(int action, char *message)
{
    char buf[1024];

    (void)strcpy(buf, "\"");

    if (strlen(message) < 2)
	(void)strcat(buf, "Unspecified condition");
    else
	(void)strcat(buf, message);
    (void)strcat(buf, "\"");
    
    push_number(action);
    push_string(buf, STRING_MSTRING);
    push_number(current_line);
    push_string(current_file, STRING_MSTRING);
    (void)apply_master_ob(M_PARSE_EXCEPTION, 4);
    
    if (action == ERROR)
	lexerror("Parse aborted on #error statement,");
}

static void
skip_comment(void)
{
    int c;

    for (;;)
    {
	while ((c = mygetc()) != '*')
	{
	    if (c == EOF)
	    {
		lexerror("End of file in a comment");
		return;
	    }
	    if (c == '\n') 
	    {
		nexpands=0;
		store_line_number_info(current_incfile, current_line);
		current_line++;
	    }
	}
	do 
	{
	    if ((c = mygetc()) == '/')
		return;
	    if (c == '\n') 
	    {
		nexpands=0;
		store_line_number_info(current_incfile, current_line);
		current_line++;
	    }
	} while (c == '*');
    }
}

static void
skip_comment2(void)
{
    int c;

    while ((c = mygetc()) != '\n' && c != EOF)
	;
    if (c == EOF) {
	lexerror("End of file in a // comment");
	return;
    }
    nexpands=0;
    store_line_number_info(current_incfile, current_line);
    current_line++;
}

#define TRY(c, t) if (gobble(c)) return t

static void
deltrail(char *ap)
{
    char *p = ap;
    if (!*p)
    {
	lexerror("Illegal # command");
    }
    else
    {
	while (*p && !isspace(*p))
	    p++;
	*p = 0;
    }
}

#define SAVEC \
    if (yyp < yytext+MAXLINE-5)\
       *yyp++ = c;\
    else {\
       lexerror("Line too long");\
       break;\
    }

static void 
handle_pragma(char *str)
{
    if (strcmp(str, "strict_types") == 0)
	pragma_strict_types = 1;
    else if (strcmp(str, "save_binary") == 0)
	;
    else if (strcmp(str, "no_clone") == 0)
	pragma_no_clone = 1;
    else if (strcmp(str, "no_inherit") == 0)
	pragma_no_inherit = 1;
    else if (strcmp(str, "no_shadow") == 0)
	pragma_no_shadow = 1;
    else if (strcmp(str, "resident") == 0)
	pragma_resident = 1;
    else
	handle_exception(WARNING, "Unknown pragma");
}

static struct keyword {
    char *word;
    short  token;
    short min_args; /* Minimum number of arguments. */
    short max_args; /* Maximum number of arguments. */
    short ret_type; /* The return type used by the compiler. */
    unsigned char arg_type1;    /* Type of argument 1 */
    unsigned char arg_type2;    /* Type of argument 2 */
    unsigned char arg_index;
            /* Index pointing to where to find arg type */
    short Default;      /* an efun to use as default for last argument */
} predefs[] =
#include "efun_defs.c"

static struct keyword reswords[] = {
{ "break",      F_BREAK, },
{ "case",       F_CASE, },
{ "catch",      F_CATCH, },
{ "continue",       F_CONTINUE, },
{ "default",        F_DEFAULT, },
{ "do",         F_DO, },
{ "else",       F_ELSE, },
{ "float",      F_FLOAT, },
{ "for",        F_FOR, },
{ "foreach",	F_FOREACH, },
{ "function",       F_FUNCTION, },
{ "if",         F_IF, },
{ "inherit",        F_INHERIT, },
{ "int",        F_INT, },
{ "mapping",        F_MAPPING, },
{ "mixed",      F_MIXED, },
{ "nomask",     F_NO_MASK, },
{ "object",     F_OBJECT, },
{ "operator",       F_OPERATOR, },
{ "parse_command",  F_PARSE_COMMAND, },
{ "private",        F_PRIVATE, },
{ "public",     F_PUBLIC, },
{ "return",     F_RETURN, },
{ "sscanf",     F_SSCANF, },
{ "static",     F_STATIC, },
{ "status",     F_STATUS, },
{ "string",     F_STRING_DECL, },
{ "switch",     F_SWITCH, },
{ "throw",	F_THROW },
{ "try",        F_TRY, },
{ "varargs",        F_VARARGS, },
{ "void",       F_VOID, },
{ "while",      F_WHILE, },
};

struct instr instrs[EFUN_LAST - EFUN_FIRST + 1];

static int
lookupword(char *s, struct keyword *words, int h)
{
    int i, l, r;
    
    l = 0;
    for (;;)
    {
	i = (l + h) / 2;
	r = strcmp(s, words[i].word);
	if (r == 0)
	    return words[i].token;
	else if (l == i)
	    return -1;
	else if (r < 0)
	    h = i;
	else
	    l = i;
    }
}

static INLINE int
lookup_resword(char *s)
{
    return lookupword(s, reswords, NELEM(reswords));
}

static int
yylex1(void)
{
    register char *yyp;
    register int c;
    register int c1, c2;
    
    for (;;)
    {
	if (lex_fatal)
	{
	    return -1;
	}
	switch(c = mygetc())
	{
	case EOF:
	    if (inctop)
	    {
		struct incstate *p;
		p = inctop;
		(void)fclose(yyin);
		/*(void)fprintf(stderr, "popping to %s\n", p->file);*/
		free(current_file);
		nexpands = 0;
		current_file = p->file;
		current_line = p->line + 1;
		current_incfile = p->incfnum;
		pragma_strict_types = p->pragma_strict_types;
		yyin = p->yyin;
		slast = p->slast;
		lastchar = p->lastchar;
		inctop = p->next;
		
		if (p->nbuf)
		{
		    nbuf = p->nbuf;
		    outp = defbuf + DEFMAX - nbuf;
		    memcpy(outp, p->outp, nbuf);
		    free((char *)p->outp);
		}
		else
		{
		    nbuf = 0;
		    outp = defbuf + DEFMAX;
		}
		
		store_line_number_info(current_incfile, current_line);
		incdepth--;
		
		free((char *)p);
		break;
	    }
	    if (iftop)
	    {
		struct ifstate *p = iftop;
		lexerror(p->state == EXPECT_ENDIF ? "Missing #endif" : "Missing #else");
		while (iftop)
		{
		    p = iftop;
		    iftop = p->next;
		    free((char *)p);
		}
	    }
	    return -1;
	case '\n':
	{
	    nexpands=0;
	    store_line_number_info(current_incfile, current_line);
	    current_line++;
	    total_lines++;
	}
        /* FALLTHROUGH */
	case ' ':
	case '\t':
	case '\f':
	case '\v':
	    break;
	case '+':
	    TRY('+', F_INC);
	    TRY('=', F_ADD_EQ);
	    return c;
	case '-':
	    TRY('>', F_ARROW);
	    TRY('-', F_DEC);
	    TRY('=', F_SUB_EQ);
	    return c;
	case '&':
	    TRY('&', F_LAND);
	    TRY('=', F_AND_EQ);
	    return c;
	case '|':
	    TRY('|', F_LOR);
	    TRY('=', F_OR_EQ);
	    return c;
	case '^':
	    TRY('=', F_XOR_EQ);
	    return c;
	case '<':
	    if (gobble('<')) {
		TRY('=', F_LSH_EQ);
		return F_LSH;
	    }
	    TRY('=', F_LE);
	    return c;
	case '>':
	    if (gobble('>'))
	    {
		TRY('=', F_RSH_EQ);
		return F_RSH;
	    }
	    TRY('=', F_GE);
	    return c;
	case '*':
	    TRY('=', F_MULT_EQ);
	    return c;
	case '%':
	    TRY('=', F_MOD_EQ);
	    return F_MOD;
	case '/':
	    if (gobble('*'))
	    {
		skip_comment();
		break;
	    }
	    else if (gobble('/'))
	    {
		skip_comment2();
		break;
	    }
	    TRY('=', F_DIV_EQ);
	    return c;
	case '=':
	    TRY('=', F_EQ);
	    return c;
	case ';':
	case '(':
	case ')':
	case ',':
	case '{':
	case '}':
	case '~':
	case '[':
	case ']':
	case '?':
	case '@':
	    return c;
	case '!':
	    TRY('=', F_NE);
	    return F_NOT;
	case ':':
	    TRY(':', F_COLON_COLON);
	    return ':';
	case '.':
	    if (gobble('.'))
	    {
		if (gobble('.'))
		    return F_VARARG;
		else
		    return F_RANGE;
	    }
	    return c;
	case '#':
	    if (lastchar == '\n') 
	    {
		char *ssp = 0;
		int quote;
		
		yyp = yytext;
		do 
		{
		    c = mygetc();
		} while (isspace(c));
		
		for (quote = 0;;) 
		{
		    if (c == '"')
			quote ^= 1;
		    
		    /*gc - handle comments cpp-like! 1.6.91 @@@*/
		    while (!quote && c == '/')  
		    {
			if (gobble('*')) 
			{ 
			    skip_comment();
			    c = mygetc();
			}
			else 
			    break;
		    }
		    
		    if (!ssp && isspace(c))
			ssp = yyp;
		    if (c == '\n' || c == EOF)
			break;
		    SAVEC;
		    c = mygetc();
		}
		if (ssp) 
		{
		    *ssp++ = 0;
		    while (isspace(*ssp))
			ssp++;
		} 
		else 
		{
		    ssp = yyp;
		}
		*yyp = 0;
		if (strcmp("define", yytext) == 0) 
		{
		    handle_define(ssp);
		} 
		else if (strcmp("if", yytext) == 0) 
		{
#if 0
		    short int nega=0; /*@@@ allow #if !VAR gc 1.6.91*/
		    if (*ssp=='!'){ ssp++; nega=1;}
		    if (isdigit(*ssp))
		    {
			char *p;
			long l;
			l = strtol(ssp, &p, 10);
			while (isspace(*p))
			    p++;
			if (*p)
			    lexerror("Condition too complex in #if");
			else
			    handle_cond(nega ? !(int)l : (int)l);
		    }
		    else if (isalunum(*ssp))
		    {
			char *p = ssp;
			while (isalunum(*p))
			    p++;
			if (*p)
			{
			    *p++ = 0;
			    while (isspace(*p))
				p++;
			}
			if (*p)
			    lexerror("Condition too complex in #if");
			else
			{
			    struct defn *d;
			    d = lookup_define(ssp);
			    if (d)
			    {
				handle_cond(nega ? !atoi(d->exps) : atoi(d->exps));/* a hack! */
			    }
			    else
			    {
				handle_cond(nega?1:0); /* cpp-like gc*/
			    }
			}
		    }
		    else
			lexerror("Condition too complex in #if");
#else
		    int cond;
            
		    myungetc(0);
		    add_input(ssp);
		    cond = cond_get_exp(0);
		    if (mygetc()) 
		    {
			lexerror("Condition too complex in #if");
			while (mygetc())
			    ;
		    }
		    else
			handle_cond(cond);
#endif
		}
		else if (strcmp("ifdef", yytext) == 0) 
		{
		    deltrail(ssp);
		    handle_cond(lookup_define(ssp) != 0);
		}
		else if (strcmp("ifndef", yytext) == 0)
		{
		    deltrail(ssp);
		    handle_cond(lookup_define(ssp) == 0);
		} 
		else if (strcmp("else", yytext) == 0) 
		{
		    if (iftop && iftop->state == EXPECT_ELSE) 
		    {
			struct ifstate *p = iftop;
			
			/*(void)fprintf(stderr, "found else\n");*/
			iftop = p->next;
			free((char *)p);
			(void)skip_to("endif", (char *)0);
			store_line_number_info(current_incfile, current_line);
			current_line++;
			total_lines++;
		    }
		    else
		    {
			lexerror("Unexpected #else");
		    }
		} 
		else if (strcmp("endif", yytext) == 0) 
		{
		    if (iftop && (iftop->state == EXPECT_ENDIF ||
				  iftop->state == EXPECT_ELSE)) 
		    {
			struct ifstate *p = iftop;
			
			/*(void)fprintf(stderr, "found endif\n");*/
			iftop = p->next;
			free((char *)p);
		    } 
		    else 
		    {
			lexerror("Unexpected #endif");
		    }
		} 
		else if (strcmp("undef", yytext) == 0) 
		{
		    struct defn *d;
		    
		    deltrail(ssp);
		    if ((d = lookup_define(ssp)) != NULL )
			d->undef++;
		} 
		else if (strcmp("echo", yytext) == 0) 
		{
		    (void)fprintf(stderr, "%s\n", ssp);
		} 
		else if (strcmp("include", yytext) == 0) 
		{
		    /*(void)fprintf(stderr, "including %s\n", ssp);     */
		    handle_include(ssp, 0);
		}
		else if (strcmp("pragma", yytext) == 0)
		{
		    deltrail(ssp);
		    handle_pragma(ssp);
		} 
		else if (strcmp("error", yytext) == 0)
		{
		    handle_exception(ERROR, ssp);
		}
		else if (strcmp("warning", yytext) == 0)
		{
		    handle_exception(WARNING, ssp);
		}
		else 
		{
		    lexerror("Unrecognised # directive");
		}
		myungetc('\n');
		break;
	    }
	    else
		goto badlex;
	case '\'':
	    yylval.number = mygetc();
	    if (yylval.number == '\\')
	    {
		int tmp = mygetc();
		switch (tmp)
		{
		case 'n': yylval.number = '\n'; break;
		case 't': yylval.number = '\t'; break;
		case 'b': yylval.number = '\b'; break;
		case 'a': yylval.number = '\a'; break;
		case 'v': yylval.number = '\v'; break;
		case '\'':
		case '\\':
		case '"':
		    yylval.number = tmp; break;
		default:
		    lexwarning("Bad character escape sequence");
		    yylval.number = tmp;
		    break;
		}
	    }
	    if (!gobble('\''))
		lexerror("Illegal character constant");
	    return F_NUMBER;
	case '"':
	    yyp = yytext;
	    *yyp++ = c;
	    for (;;)
	    {
		c = mygetc();
		if (c == EOF)
		{
		    lexerror("End of file in string");
		    return string("\"\"");
		}
		else if (c == '\n')
		{
		    lexerror("Newline in string");
		    return string("\"\"");
		}
		SAVEC;
		if (c == '"')
		    break;
		if (c == '\\')
		{
		    c = mygetc();
		    if ( c == '\n' )
		    {
			yyp--;
			store_line_number_info(current_incfile, current_line);
			current_line++;
			total_lines++;
		    } 
		    else if ( c == EOF ) 
		    {
			/* some operating systems give EOF only once */
			myungetc(c); 
		    } 
		    else
			*yyp++ = c;
		}
	    }
	    *yyp = 0;
	    return string(yytext);

	case '0':
	    c = mygetc();
	    if ( c == 'X' || c == 'x' || c == 'o') 
	    {
                char *endptr;
                long long value;
                int base = 16;
                if (c == 'o')
                    base = 8;

                
		yyp = yytext;

		for (;;) 
		{
		    c = mygetc();
		    if (!isxdigit(c))
			break;
                    SAVEC;
		}
		myungetc(c);
                *yyp = '\0';
                
                value = strtoll(yytext, &endptr, base);
                if (*endptr != '\0')
                {
                    fprintf(stderr, "%s\n", yytext);
                    lexwarning("Invalid digits in octal number number");
                }
                
                return number(value);
	    }
	    myungetc(c);
	    c = '0';
	    /* FALLTHROUGH */
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	    yyp = yytext;
	    *yyp++ = c;
	    for (;;)
	    {
		c = mygetc();
		if (!isdigit(c))
		    break;
		SAVEC;
	    }
	    if (c == '.')
	    {
		if (isdigit(c1 = mygetc()))
		{
		    SAVEC;
		    c = c1;
		    SAVEC;
		    for (c = mygetc(); isdigit(c); c = mygetc())
			SAVEC;
		    if (c == 'e' || c == 'E')
		    {
			c1 = mygetc();
			if (c1 == '-' || c1 == '+')
			{
			    c2 = mygetc();
			    if (isdigit(c2))
			    {
				SAVEC;
				c = c1;
				SAVEC;
				c = c2;
				SAVEC;
				for (c = mygetc(); isdigit(c); c = mygetc())
				    SAVEC;
			    }
			    else
			    {
				myungetc(c2);
				myungetc(c1);
			    }
			}
			else if (isdigit(c1))
			{
			    SAVEC;
			    c = c1;
			    SAVEC;
			    for (c = mygetc(); isdigit(c); c = mygetc())
				SAVEC;
			}
			else
			    myungetc(c1);
		    }
		    myungetc(c);
		    *yyp = 0;
		    return real(strtod(yytext, NULL));
		}
		myungetc(c1);
	    }
	    myungetc(c);
	    *yyp = 0;
	    if (*yytext == '0')
            {
                /* OCTALS */
                char *endptr;
                long long value;

                value = strtoll(yytext, &endptr, 010);

                if (*endptr != '\0')
                    lexwarning("Invalid digits in octal number");

                if (value != 0)
                    lexwarning("Obsolete octal format used. Use 0o111 syntax");
                
		return number(value);
            }
	    return number(atoll(yytext));
	default:
	    if (isalpha(c) || c == '_') {
		int r;
		
		yyp = yytext;
		*yyp++ = c;
		for (;;)
		{
		    c = mygetc();
		    if (!isalunum(c))
			break;
		    SAVEC;
		}
		*yyp = 0;
		
		myungetc(c);
		if (!expand_define())
		{
		    r = lookup_resword(yytext);
		    if (r >= 0)
		    {
			return r;
		    }
		    else
			return ident(yytext);
		}
		break;
	    }
	    goto badlex;
	}
    }
  badlex:
    {
	char buff[100]; (void)sprintf(buff, "Illegal character (hex %02x) '%c'", c, c);
	lexerror(buff); return ' '; }
}

int
yylex(void)
{
    int r;

    if (keep1.token != -47)
    {
	/*
	 * something in keep buffer. shift it out.
	 */
	r = keep1.token;
	current_line = keep1.line;
	yylval = keep1.lval;
	keep1 = keep2;
	keep2 = keep3;
	keep3.token = -47;
	return r;
    }
    yytext[0] = 0;
    /*
     * see if the next token is a string
     */
    if ((r = yylex1()) == F_STRING)
    {
	keep4.lval = yylval;
	keep4.line = current_line;
	yytext[0] = 0;
	r = yylex1();
	for (;;)
	{
	    keep1.line = current_line;
	    keep1.lval = yylval;
	    if (r != '+')
	    {
		/*
		 * 1:string 2:non-'+'
		 *  save 2, return 1
		 */
		keep1.token = r;
		yylval = keep4.lval;
		current_line = keep4.line;
		return F_STRING;
	    }
	    yytext[0] = 0;
	    r = yylex1();
	    keep2.line = current_line;
	    keep2.lval = yylval;
	    if (r != F_STRING)
	    {
		/*
		 * 1:string 2:'+' 3:non-string
		 *  save 2 and 3, return 1
		 */
		keep1.token = '+';
		keep2.token = r;
		current_line = keep4.line;
		yylval = keep4.lval;
		return F_STRING;
	    }
	    yytext[0] = 0;
	    r = yylex1();
	    keep3.line = current_line;
	    keep3.lval = yylval;
	    if (r == '[' || r == F_ARROW)
	    {
		/*
		 * 1:string 2:'+' 3:string 4:[->
		 *  save 2, 3, 4, return 1
		 */
		keep1.token = '+';
		keep2.token = F_STRING;
		keep3.token = r;
		current_line = keep4.line;
		yylval = keep4.lval;
		return F_STRING;
	    }
	    /*
	     * concatenate string constants
	     */
	    keep3.lval.string = tmpalloc(strlen(keep4.lval.string) +
					 strlen(keep2.lval.string) + 1);
	    (void)strcpy(keep3.lval.string, keep4.lval.string);
	    (void)strcat(keep3.lval.string, keep2.lval.string);
	    keep4.line = keep2.line;
	    keep4.lval.string = keep3.lval.string;
	}
    }
    /*    (void)fprintf(stderr, "lex=%d(%s) ", r, yytext);*/
    return r;
}
 
extern YYSTYPE yylval;

static int 
ident(char *str)
{
    yylval.string = tmpstring_copy(str);
    return F_IDENTIFIER;
}

static int 
string(char *str)
{
    char *p;

    if (!*str)
    {
	str = "\"\"";
    }
    p = tmpalloc(strlen(str) + 1);
    yylval.string = p;
    for (str++; str[0] && str[1] ; str++, p++)
    {
	/* Copy the similar one to here /JH */
	if (str[0] == '\\') {
	    if (str[1] == 'n') {
		*p = '\n';
	    } else if (str[1] == 't') {
		*p = '\t';
	    } else if (str[1] == 'r') {
		*p = '\r';
	    } else if (str[1] == 'b') {
		*p = '\b';
	    } else if (str[1] == 'a') {
		*p = '\a';
	    } else if (str[1] == 'v') {
		*p = '\v';
	    } else if (str[1] == '"' || str[1] == '\\' || str[1] == '\'') {
		*p = str[1];
	    } else {
		lexwarning("Bad string escape sequence.");
		*p = str[1];
	    }
	    str++;
	} else
	    *p = *str;
    }
    *p = '\0';
    return F_STRING;
}

static int 
number(long long i)
{
    yylval.number = i;
    return F_NUMBER;
}

static int
real(double f)
{
    yylval.real = f;
    return F_FLOATC;
}

void 
end_new_file(void)
{
    while (inctop)
    {
	struct incstate *p;
	p = inctop;
	(void)fclose(yyin);
	free(current_file);
	current_file = p->file;
	yyin = p->yyin;
	inctop = p->next;
	
	if (p->outp != NULL)
	{
	    free((char *)p->outp);
	}
	
	free((char *)p);
    }
    while (iftop)
    {
	struct ifstate *p;
	
	p = iftop;
	iftop = p->next;
	free((char *)p);
    }
    free_defines();
}

/*
 * Function name : ltoa
 * Description   : function converts integer into it's text representation
 * Arguments     : int       - the integer number to be converted
 * Returns       : char*     - the integer textual representation
 */
static inline char*
ltoa(long long intval)
{
    static char buffer[25];

    sprintf(buffer, "%lld", intval);
    return buffer;
}

void
start_new_file(FILE *f)
{
    struct lpc_predef_s *tmpf;
    free_defines();
    add_define("_FUNCTION", -1, "");
    add_define("LPC4", -1, "");   /* Tell coders this is LPC ver 4.0 */
    add_define("CD_DRIVER", -1, "");
#ifdef DEBUG
    add_define("DEBUG_DRIVER", -1, "");
#endif
    add_define("T_INTEGER" , -1, ltoa(T_NUMBER));
    add_define("T_FLOAT"   , -1, ltoa(T_FLOAT));
    add_define("T_STRING"  , -1, ltoa(T_STRING));
    add_define("T_OBJECT"  , -1, ltoa(T_OBJECT));
    add_define("T_FUNCTION", -1, ltoa(T_FUNCTION));
    add_define("T_ARRAY"   , -1, ltoa(T_POINTER));
    add_define("T_MAPPING" , -1, ltoa(T_MAPPING));

    for (tmpf = lpc_predefs; tmpf; tmpf = tmpf->next) 
    {
	char namebuf[NSIZE];
	char mtext[MLEN];
	
	*mtext='\0';
	(void)sscanf(tmpf->flag, "%[^=]=%[ -~=]", namebuf, mtext);
	if (strlen(namebuf) >= NSIZE)
	    fatal("NSIZE exceeded\n");
	if (strlen(mtext) >= MLEN)
	    fatal("MLEN exceeded\n");
	add_define(namebuf,-1,mtext);
    }
    keep1.token = keep2.token = keep3.token = -47;
    yyin = f;
    slast = '\n';
    lastchar = '\n';
    inctop = 0;         /* If not here, where? */
    num_incfiles = 0;
    current_incfile = 0;
    current_line = 1;
    lex_fatal = 0;
    incdepth = 0;
    nbuf = 0;
    outp = defbuf+DEFMAX;
    pragma_strict_types = 0;        
    pragma_no_inherit = pragma_no_clone = pragma_no_shadow = pragma_resident = 0;
    nexpands = 0;
}

/*
 * The number of arguments stated below, are used by the compiler.
 * If min == max, then no information has to be coded about the
 * actual number of arguments. Otherwise, the actual number of arguments
 * will be stored in the byte after the instruction.
 * A maximum value of -1 means unlimited maximum value.
 *
 * If an argument has type 0 (T_INVALID) specified, then no checks will
 * be done at run time.
 *
 * The argument types are currently not checked by the compiler,
 * only by the runtime.
 */

static void 
add_instr_name(char *name, int n)
{
    instrs[n - EFUN_FIRST].name = name;
}

#define T_ANY T_STRING|T_NUMBER|T_POINTER|T_OBJECT|T_MAPPING|T_FLOAT|T_FUNCTION
static void 
add_instr_opname(char *name, int n, int ret, int arg1, int arg2)
{
    n -= EFUN_FIRST;
    instrs[n].name = name;
    instrs[n].min_arg = 2;
    instrs[n].max_arg = 2;
    instrs[n].type[0] = arg1;
    instrs[n].type[1] = arg2;
    instrs[n].ret_type = ret;
}

static void 
add_instr_opname_prefix(char *name, int n, int ret, int arg)
{
    n -= EFUN_FIRST;
    instrs[n].name = name;
    instrs[n].min_arg = 1;
    instrs[n].max_arg = 1;
    instrs[n].type[0] = arg;
    instrs[n].type[1] = 0;
    instrs[n].ret_type = ret;
}

void 
init_num_args(void)
{
    int i, n;

    for (i = 0; i<NELEM(predefs); i++)
    {
	n = predefs[i].token - EFUN_FIRST;
	if (n < 0 || n > NELEM(instrs))
	    fatal("Token %s has illegal value %d.\n", predefs[i].word, n);
	instrs[n].min_arg = predefs[i].min_args;
	instrs[n].max_arg = predefs[i].max_args;
	instrs[n].name = predefs[i].word;
	instrs[n].type[0] = predefs[i].arg_type1;
	instrs[n].type[1] = predefs[i].arg_type2;
	instrs[n].Default = predefs[i].Default;
	instrs[n].ret_type = predefs[i].ret_type;
	instrs[n].arg_index = predefs[i].arg_index;
    }
    add_instr_opname("+", F_ADD, TYPE_ANY, T_ANY, T_ANY);
    add_instr_opname("-", F_SUBTRACT, TYPE_ANY, T_ANY, T_ANY);
    add_instr_opname("*", F_MULTIPLY, TYPE_ANY, T_ANY, T_ANY);
    add_instr_opname("/", F_DIVIDE, TYPE_ANY, T_ANY, T_ANY);
    add_instr_opname("%", F_MOD, TYPE_ANY, T_ANY, T_ANY);
    add_instr_opname("&", F_AND, TYPE_ANY, T_ANY, T_ANY);
    add_instr_opname("|", F_OR, TYPE_ANY, T_ANY, T_ANY);
    add_instr_opname("^", F_XOR, TYPE_NUMBER, T_NUMBER, T_NUMBER);
    add_instr_opname(">>", F_LSH, TYPE_NUMBER, T_NUMBER, T_NUMBER);
    add_instr_opname("<<", F_RSH, TYPE_NUMBER, T_NUMBER, T_NUMBER);
    add_instr_opname("<", F_LT, TYPE_NUMBER, T_ANY, T_ANY);
    add_instr_opname(">", F_GT, TYPE_NUMBER, T_ANY, T_ANY);
    add_instr_opname("<=", F_LE, TYPE_NUMBER, T_ANY, T_ANY);
    add_instr_opname(">=", F_GE, TYPE_NUMBER, T_ANY, T_ANY);
    add_instr_opname("==", F_EQ, TYPE_NUMBER, T_ANY, T_ANY);
    add_instr_opname("!=", F_NE, TYPE_NUMBER, T_ANY, T_ANY);
    add_instr_opname("index", F_INDEX, TYPE_ANY, T_ANY, T_ANY);
    add_instr_opname_prefix("!", F_NOT, TYPE_NUMBER, T_ANY);
    add_instr_opname_prefix("~", F_COMPL, TYPE_NUMBER, T_NUMBER);

    add_instr_name("+=", F_ADD_EQ);
    add_instr_name("-=", F_SUB_EQ);
    add_instr_name("/=", F_DIV_EQ);
    add_instr_name("*=", F_MULT_EQ);
    add_instr_name("%=", F_MOD_EQ);
    add_instr_name("&=", F_AND_EQ);
    add_instr_name("|=", F_OR_EQ);
    add_instr_name("^=", F_XOR_EQ);
    add_instr_name("<<=", F_LSH_EQ);
    add_instr_name(">>=", F_RSH_EQ);
    add_instr_name("push_indexed_lvalue", F_PUSH_INDEXED_LVALUE);
    add_instr_name("identifier", F_IDENTIFIER);
    add_instr_name("local", F_LOCAL_NAME);
    add_instr_name("indirect", F_INDIRECT);
    add_instr_name("number", F_NUMBER);
    add_instr_name("push_local_variable_lvalue", F_PUSH_LOCAL_VARIABLE_LVALUE);
    add_instr_name("const1", F_CONST1);
    add_instr_name("subtract", F_SUBTRACT);
    add_instr_name("assign", F_ASSIGN);
    add_instr_name("pop", F_POP_VALUE);
    add_instr_name("const0", F_CONST0);
    add_instr_name("floatc", F_FLOATC);
    add_instr_name("jump_when_zero", F_JUMP_WHEN_ZERO);
    add_instr_name("jump_when_non_zero", F_JUMP_WHEN_NON_ZERO);
    add_instr_name("||", F_LOR);
    add_instr_name("&&", F_LAND);
    add_instr_name("jump", F_JUMP);
    add_instr_name("return", F_RETURN);
    add_instr_name("sscanf", F_SSCANF);
    add_instr_name("string", F_STRING);
    add_instr_name("call_non_virtual", F_CALL_NON_VIRT);
    add_instr_name("call_virtual", F_CALL_VIRT);
    add_instr_name("call_var", F_CALL_VAR);
    add_instr_name("call_simul", F_CALL_SIMUL);
    add_instr_name("papply", F_PAPPLY);
    add_instr_name("build_closure", F_BUILD_CLOSURE);
    add_instr_name("aggregate", F_AGGREGATE);
    add_instr_name("m_aggregate", F_M_AGGREGATE);
    add_instr_name("push_identifier_lvalue", F_PUSH_IDENTIFIER_LVALUE);
    add_instr_name("dup", F_DUP);
    add_instr_name("catch", F_CATCH);
    add_instr_name("neg", F_NEGATE);
    add_instr_name("x++", F_POST_INC);
    add_instr_name("x--", F_POST_DEC);
    add_instr_name("++x", F_INC);
    add_instr_name("--x", F_DEC);
    add_instr_name("switch",F_SWITCH);
    add_instr_name("break",F_BREAK);
    add_instr_name("range",F_RANGE);
    add_instr_name("foreach", F_FOREACH);
    add_instr_name("foreach", F_FOREACH_M);
    instrs[F_RANGE-EFUN_FIRST].type[0] = T_POINTER|T_STRING;
}
#undef T_ANY

char *
get_f_name(int n)
{
    if (instrs[n-EFUN_FIRST].name)
	return instrs[n-EFUN_FIRST].name;
    else
    {
	static char buf[30];
	(void)sprintf(buf, "<OTHER %d>", n);
	return buf;
    }
}



INLINE int lookup_predef(char *s)
{
    return lookupword(s, predefs, NELEM(predefs));
}

#define NARGS 25
#define MARKS '@'

#define SKIPWHITE while (isspace(*p)) p++
#define GETALPHA(p, q, m) \
    while (isalunum(*p)) {\
    *q = *p++;\
    if (q < (m))\
        q++;\
    else {\
        lexerror("Name too long");\
        return;\
    }\
    }\
    *q++ = 0

static int
cmygetc(void)
{
    int c;

    for (;;)
    {
	c = mygetc();
	if (c == '/')
	{
	    if (gobble('*'))
		skip_comment();
	    else
		return c;
	} else
	    return c;
    }
}

static void
refill(void)
{
    char *p;
    int c;

    p = yytext;
    do
    {
	c = cmygetc();
	if (p < yytext+MAXLINE-5)
	    *p++ = c;
	else
	{
	    lexerror("Line too long");
	    break;
	}
    } while (c != '\n' && c != EOF);
    p[-1] = ' ';
    *p = 0;
    nexpands=0;
    store_line_number_info(current_incfile, current_line);
    current_line++;
}

static void
handle_define(char *yyt)
{
    char namebuf[NSIZE];
    char args[NARGS][NSIZE];
    char mtext[MLEN];
    char *p, *q;

    p = yyt;
    (void)strcat(p, " ");
    q = namebuf;
    GETALPHA(p, q, namebuf+NSIZE-1);
    if (*p == '(')
    {       /* if "function macro" */
	int arg;
	int inid;
	char *ids = 0;
	p++;            /* skip '(' */
	SKIPWHITE;
	if (*p == ')')
	{
	    arg = 0;
	}
	else
	{
	    for (arg = 0; arg < NARGS; )
	    {
		q = args[arg];
		GETALPHA(p, q, args[arg] + NSIZE - 1);
		arg++;
		SKIPWHITE;
		if (*p == ')')
		    break;
		if (*p++ != ',')
		{
		    lexerror("Missing ',' in #define parameter list");
		    return;
		}
		SKIPWHITE;
	    }
	    if (arg == NARGS)
	    {
		lexerror("Too many macro arguments");
		return;
	    }
	}
	p++;            /* skip ')' */
	for (inid = 0, q = mtext; *p; )
	{
	    if (isalunum(*p))
	    {
		if (!inid)
		{
		    inid++;
		    ids = p;
		}
	    }
	    else
	    {
		if (inid)
		{
		    size_t l, idlen = p - ids;
		    int n;
		    
		    for (n = 0; n < arg; n++)
		    {
			l = strlen(args[n]);
			if (l == idlen && strncmp(args[n], ids, l) == 0)
			{
			    q -= idlen;
			    *q++ = MARKS;
			    *q++ = n+MARKS+1;
			    break;
			}
		    }
		    inid = 0;
		}
	    }
	    *q = *p;
	    if (*p++ == MARKS)
		*++q = MARKS;
	    if (q < mtext + MLEN - 2)
		q++;
	    else
	    {
		lexerror("Macro text too long");
		return;
	    }
	    if (!*p && p[-2] == '\\')
	    {
		q -= 2;
		refill();
		p = yytext;
	    }
	}
	*--q = 0;
	add_define(namebuf, arg, mtext);
    }
    else
    {
	for (q = mtext; *p; )
	{
	    *q = *p++;
	    if (q < mtext + MLEN - 2)
		q++;
	    else
	    {
		lexerror("Macro text too long");
		return;
	    }
	    if (!*p && p[-2] == '\\')
	    {
		q -= 2;
		refill();
		p = yytext;
	    }
	}
	*--q = 0;
	add_define(namebuf, -1, mtext);
    }
    return;
}

static void
myungetc(int c)
{
    *--outp = c;
    nbuf++;
}

static void
add_input(char *p)
{
    size_t l = strlen(p);

    if (nbuf+l >= DEFMAX-10)
    {
	lexerror("Macro expansion buffer overflow");
	return;
    }
    outp -= l;
    nbuf += l;
    (void)strncpy(outp, p, l);
}

#define DEFHASH 1999
struct defn *defns[DEFHASH];
#define defhash(s) hashstr(s, 10, DEFHASH)

void
add_pre_define(char *define)
{
    struct lpc_predef_s *tmp;
    
    tmp = (struct lpc_predef_s *)xalloc(sizeof(struct lpc_predef_s));
    
    tmp->flag = string_copy(define);
    tmp->next = lpc_predefs;
    lpc_predefs = tmp;
}

static void
add_define(char *name, int nargs, char *exps)
{
    struct defn *p;
    int h;

    if ((p = lookup_define(name)) != NULL)
    {
	if (nargs != p->nargs || strcmp(exps, p->exps) != 0)
	{
	    char buf[200+NSIZE];
	    (void)sprintf(buf, "Redefinition of #define %s", name);
	    lexerror(buf);
	}
	return;
    }
    p = (struct defn *)xalloc(sizeof(struct defn));
    p->name = xalloc(strlen(name)+1);
    (void)strcpy(p->name, name);
    p->undef = 0;
    p->nargs = nargs;
    p->exps = xalloc(strlen(exps)+1);
    (void)strcpy(p->exps, exps);
    h = defhash(name);
    p->next = defns[h];
    defns[h] = p;
    /*(void)fprintf(stderr, "define '%s' %d '%s'\n", name, nargs, exps);*/
}

static void
free_defines(void)
{
    struct defn *p, *q;
    int i;

    for (i = 0; i < DEFHASH; i++)
    {
	for (p = defns[i]; p; p = q)
	{
	    q = p->next;
	    free(p->name);
	    free(p->exps);
	    free((char *)p);
	}
	defns[i] = 0;
    }
    nexpands = 0;
}

struct defn *
lookup_define(char *s)
{
    struct defn *p;
    int h;

    h = defhash(s);
    for (p = defns[h]; p; p = p->next)
	if (!p->undef && strcmp(s, p->name) == 0)
	    return p;
    return 0;
}

#define SKIPW \
        do {\
        c = cmygetc();\
    } while (isspace(c));


/* Check if yytext is a macro and expand if it is. */
static int
expand_define(void)
{
    struct defn *p;
    char expbuf[DEFMAX];
    char *args[NARGS];
    char buf[DEFMAX];
    char *q, *e, *b;

    if (nexpands++ > EXPANDMAX)
    {
	lexerror("Too many macro expansions");
	return 0;
    }
    p = lookup_define(yytext);
    if (!p)
    {
	return 0;
    }
    if (p->nargs == -1)
    {
	add_input(p->exps);
    }
    else
    {
	int c, parcnt = 0, dquote = 0, squote = 0;
	int n;
	SKIPW;
	if (c != '(')
	{
	    lexerror("Missing '(' in macro call");
	    return 0;
	}
	SKIPW;
	if (c == ')')
	    n = 0;
	else
	{
	    q = expbuf;
	    args[0] = q;
	    for (n = 0; n < NARGS; )
	    {
		switch(c)
		{
		case '"':
		    if (!squote)
			dquote ^= 1;
		    break;
		case '\'':
		    if (!dquote)
			squote ^= 1;
		    break;
		case '(':
		    if (!squote && !dquote)
			parcnt++;
		    break;
		case ')':
		    if (!squote && !dquote)
			parcnt--;
		    break;
		case '\\':
		    if (squote || dquote)
		    {
			*q++ = c;
			c = mygetc();}
		    break;
		case '\n':
		    if (squote || dquote)
		    {
			lexerror("Newline in string");
			return 0;
		    }
		    break;
		}
		if (c == ',' && !parcnt && !dquote && !squote)
		{
		    *q++ = 0;
		    args[++n] = q;
		}
		else if (parcnt < 0)
		{
		    *q++ = 0;
		    n++;
		    break;
		}
		else
		{
		    if (c == EOF)
		    {
			lexerror("Unexpected end of file");
			return 0;
		    }
		    if (q >= expbuf + DEFMAX - 5)
		    {
			lexerror("Macro argument overflow");
			return 0;
		    }
		    else
		    {
			*q++ = c;
		    }
		}
		if (!squote && ! dquote)
		    c = cmygetc();
		else
		    c = mygetc();
	    }
	    if (n == NARGS)
	    {
		lexerror("Maximum macro argument count exceeded");
		return 0;
	    }
	}
	if (n != p->nargs)
	{
	    lexerror("Wrong number of macro arguments");
	    return 0;
	}
	/* Do expansion */
	b = buf;
	e = p->exps;
	while (*e)
	{
	    if (*e == MARKS)
	    {
		if (*++e == MARKS)
		    *b++ = *e++;
		else
		{
		    for (q = args[*e++ - MARKS - 1]; *q; )
		    {
			*b++ = *q++;
			if (b >= buf+DEFMAX)
			{
			    lexerror("Macro expansion overflow");
			    return 0;
			}
		    }
		}
	    }
	    else
	    {
		*b++ = *e++;
		if (b >= buf+DEFMAX)
		{
		    lexerror("Macro expansion overflow");
		    return 0;
		}
	    }
	}
	*b++ = 0;
	add_input(buf);
    }
    return 1;
}

/* Stuff to evaluate expression.  I havn't really checked it. /LA
** Written by "J\"orn Rennecke" <amylaar@cs.tu-berlin.de>
*/
static int
exgetc(void)
{
    register char c, *yyp;
    
    c = mygetc();
    while (isalpha(c) || c == '_')
    {
	yyp = yytext;
	do
	{
	    SAVEC;
	    c = mygetc();
	} while (isalunum(c));
	myungetc(c);
	*yyp = '\0';
	if (strcmp(yytext, "defined") == 0)
	{
	    /* handle the defined "function" in #if */
	    do
		c = mygetc();
	    while (isspace(c));
	    
	    if (c != '(')
	    {
		lexerror("Missing ( in defined");
		continue;
	    }
	    do
		c = mygetc();
	    while (isspace(c));
	    
	    yyp = yytext;
	    while (isalunum(c))
	    {
		SAVEC;
		c = mygetc();
	    }
	    *yyp = '\0';
	    while (isspace(c))
		c = mygetc();
	    if (c != ')')
	    {
		lexerror("Missing ) in defined");
		continue;
	    }

	    /* Skip whitespace */
	    do
		c = mygetc();
	    while (isspace(c)); 
	    myungetc(c);

	    if (lookup_define(yytext))
		add_input(" 1 ");
	    else
		add_input(" 0 ");
	}
	else
	{
	    if (!expand_define()) add_input(" 0 ");
	}
	c = mygetc();
    }
    return c;
}

#define BNOT   1
#define LNOT   2
#define UMINUS 3
#define UPLUS  4

#define MULT   1
#define DIV    2
#define MOD    3
#define BPLUS  4
#define BMINUS 5
#define LSHIFT 6
#define RSHIFT 7
#define LESS   8
#define LEQ    9
#define GREAT 10
#define GEQ   11
#define EQ    12
#define NEQ   13
#define BAND  14
#define XOR   15
#define BOR   16
#define LAND  17
#define LOR   18
#define QMARK 19

static char _optab[]=
{0,4,0,0,0,26,56,0,0,0,18,14,0,10,0,22,0,0,0,0,0,0,0,0,0,0,0,0,30,50,40,74,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,70,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,63,0,1};
static char optab2[]=
{BNOT,0,0,LNOT,'=',NEQ,7,0,0,UMINUS,0,BMINUS,10,UPLUS,0,BPLUS,10,
0,0,MULT,11,0,0,DIV,11,0,0,MOD,11,
0,'<',LSHIFT,9,'=',LEQ,8,0,LESS,8,0,'>',RSHIFT,9,'=',GEQ,8,0,GREAT,8,
0,'=',EQ,7,0,0,0,'&',LAND,3,0,BAND,6,0,'|',LOR,2,0,BOR,4,
0,0,XOR,5,0,0,QMARK,1};

static int 
cond_get_exp(int priority)
{
    int c;
    int value, value2, x;
    
    do
	c = exgetc();
    while (isspace(c));
    
    if (c == '(')
    {
	value = cond_get_exp(0);
	do
	    c = exgetc();
	while (isspace(c));
	if (c != ')')
	{
	    lexerror("bracket not paired in #if");
	    if (!c)
		myungetc('\0');
	}
    }
    else if (ispunct(c))
    {
	x = (_optab-' ')[c];
	if (!x)
	{
	    lexerror("illegal character in #if");
	    return 0;
	}
	value = cond_get_exp(12);
	switch ( optab2[x-1] )
	{
	case BNOT  : value = ~value; break;
	case LNOT  : value = !value; break;
	case UMINUS: value = -value; break;
	case UPLUS : value =  value; break;
	default :
	    lexerror("illegal unary operator in #if");
	    return 0;
	}
    }
    else
    {
	int base;
	
	if (!isdigit(c))
	{
	    if (!c)
	    {
		lexerror("missing expression in #if");
		myungetc('\0');
	    }
	    else
		lexerror("illegal character in #if");
	    return 0;
	}
	value = 0;
	if (c!='0')
	    base = 10;
	else
	{
	    c = mygetc();
	    if ( c == 'x' || c == 'X' )
	    {
		base = 16;
		c = mygetc();
	    }
	    else
		base=8;
	}
	for (;;)
	{
	    if ( isdigit(c) )
		x = -'0';
	    else if (isupper(c))
		x = -'A' + 10;
	    else if (islower(c))
		x = -'a' + 10;
	    else
		break;
	    x += c;
	    if (x > base)
		break;
	    value = value*base + x;
	    c = mygetc();
	}
	myungetc(c);
    }
    for (;;)
    {
	do
	    c = exgetc();
	while (isspace(c));
	
	if (!ispunct(c))
	    break;
	x = (_optab-' ')[c];
	if (!x)
	    break;
	value2 = mygetc();
	for (;;x += 3)
	{
	    if (!optab2[x])
	    {
		myungetc(value2);
		if (!optab2[x + 1])
		{
		    lexerror("illegal operator use in #if");
		    return 0;
		}
		break;
	    }
	    if (value2 == optab2[x])
		break;
	}
	if (priority >= optab2[x + 2])
	{
	    if (optab2[x])
		myungetc(value2);
	    break;
	}
	value2 = cond_get_exp(optab2[x + 2]);
	switch ( optab2[x + 1] )
	{
	case MULT   : value *=  value2;                    break;
	case DIV    : value /=  value2;                    break;
	case MOD    : value %=  value2;                    break;
	case BPLUS  : value +=  value2;                    break;
	case BMINUS : value -=  value2;                    break;
	case LSHIFT : value <<= value2;                    break;
	case RSHIFT : value =   (unsigned)value >> value2; break;
	case LESS   : value =   value <  value2;           break;
	case LEQ    : value =   value <= value2;           break;
	case GREAT  : value =   value >  value2;           break;
	case GEQ    : value =   value >= value2;           break;
	case EQ     : value =   value == value2;           break;
	case NEQ    : value =   value != value2;           break;
	case BAND   : value &=  value2;                    break;
	case XOR    : value ^=  value2;                    break;
	case BOR    : value |=  value2;                    break;
	case LAND   : value =  value && value2;            break;
	case LOR    : value =  value || value2;            break;
	case QMARK  :
	    do
		c = exgetc();
	    while (isspace(c));
            if (c != ':')
	    {
		lexerror("'?' without ':' in #if");
		myungetc(c);
		return 0;
	    }
	    if (value) 
	    {
		(void)cond_get_exp(1);
		value = value2;
	    }
	    else
		value = cond_get_exp(1);
	    break;
	}
    }
    myungetc(c);
    return value;
}

/* Free the global list of include directories.
 */
void
free_inc_list(void)
{
    int i;

    for (i = 0; i < inc_list_size; i++)
	free(inc_list[i]);
    if (inc_list)
	free(inc_list);

}

/* Set the global list of include directories.
   Affects globals inc_list and inc_list_size */
void 
set_inc_list(struct svalue *sv)
{
    int i;
    struct vector *v;
    char *p, *end;
    char **tmp_inc_list;
    
    free_inc_list();
    if (sv == 0)
    {
	inc_list_size = 0;
	inc_list = NULL;
	return;
    }
    if (sv->type != T_POINTER)
    {
	(void)fprintf(stderr, "'define_include_dirs' in master.c did not return an array.\n");
	exit(1);
	inc_list_size = 0;
	inc_list = NULL;
	return;
    }
    v = sv->u.vec;
    if (v->size == 0) {
	(void)fprintf(stderr, "'define_include_dirs' returned an empty array\n");
	inc_list_size = 0;
	inc_list = NULL;
	return;
    }

    tmp_inc_list = (char **) xalloc(v->size * sizeof (char *));
    inc_list_size = 0;
    for (i = 0; i < v->size; i++)
    {
	if (v->item[i].type != T_STRING)
	{
	    (void)fprintf(stderr, "Illegal value returned from 'define_include_dirs' in master.c\n");
	    continue;
	}
	p = v->item[i].u.string;
	if (*p == '/')
	    p++;
	/*
	 * Even make sure that the game administrator has not made an error.
	 */
	if (!legal_path(p))
	{
	    (void)fprintf(stderr, "'define_include_dirs' must give paths without any '..'\n");
	    continue;
	}
	if ((end = strstr(p, "%s"))) {
	    if (end != p + strlen(p) - 2) {
		(void)fprintf(stderr, "'define_include_dirs' may only contain %%s last.");
		continue;
	    }
	}
	else
	    end = p + strlen(p);
	if (end != p && end[-1] != '/') {
	    tmp_inc_list[inc_list_size] = xalloc(end - p + 2);
	    strncpy(tmp_inc_list[inc_list_size], p, end - p);
	    tmp_inc_list[inc_list_size][end - p] = '/';
	    tmp_inc_list[inc_list_size][end - p + 1] = '\0';
	} else {
	    tmp_inc_list[inc_list_size] = xalloc(end - p + 1);
	    strncpy(tmp_inc_list[inc_list_size], p, end - p);
	    tmp_inc_list[inc_list_size][end - p] = '\0';
	}
	inc_list_size++;
    }
    if (inc_list_size == 0) {
	inc_list = NULL;
	free(tmp_inc_list);
    } else if (inc_list_size != v->size) {
	inc_list = (char **) xalloc(inc_list_size * sizeof (char *));
	for (i = 0; i < inc_list_size; i++)
	    inc_list[i] = tmp_inc_list[i];
	free(tmp_inc_list);
    } else
	inc_list = tmp_inc_list;
}
