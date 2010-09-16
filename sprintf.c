/*
 * sprintf.c v1.05+ for LPMud 3.0.52 (+ plus mappings)
 *
 * An implementation of (s)printf() for LPC, with quite a few
 * extensions (note that as no floating point exists, some parameters
 * have slightly different meaning or restrictions to "standard"
 * (s)printf.)  Implemented by Lynscar (Sean A Reith).
 *
 * This version supports the following as modifiers:
 *  " "   pad positive integers with a space.
 *  "+"   pad positive integers with a plus sign.
 *  "-"   left adjusted within field size.
 *        NB: std (s)printf() defaults to right justification, which is
 *            unnatural in the context of a mainly string based language
 *            but has been retained for "compatability" ;)
 *  "|"   centered within field size.
 *  "="   column mode if strings are greater than field size.  this is only
 *        meaningful with strings, all other types ignore
 *        this.  columns are auto-magically word wrapped.
 *  "#"   table mode, print a list of '\n' separated 'words' in a
 *        table within the field size.  only meaningful with strings.
 *   n    specifies the field size, a '*' specifies to use the corresponding
 *        arg as the field size.  if n is prepended with a zero, then is padded
 *        zeros, else it is padded with spaces (or specified pad string).
 *  "."n  precision of n, simple strings truncate after this (if precision is
 *        greater than field size, then field size = precision), tables use
 *        precision to specify the number of columns (if precision not specified
 *        then tables calculate a best fit), all other types ignore this.
 *  ":"n  n specifies the fs _and_ the precision, if n is prepended by a zero
 *        then it is padded with zeros instead of spaces.
 *  "@"   the argument is an array.  the corresponding format_info (minus the
 *        "@") is applyed to each element of the array.
 *  "'X'" The char(s) between the single-quotes are used to pad to field
 *        size (defaults to space) (if both a zero (in front of field
 *        size) and a pad string are specified, the one specified second
 *        overrules).  NOTE:  to include "'" in the pad string, you must
 *        use "\\'" (as the backslash has to be escaped past the
 *        interpreter), similarly, to include "\" requires "\\\\".
 * The following are the possible type specifiers.
 *  "%"   in which case no arguments are interpreted, and a "%" is inserted, and
 *        all modifiers are ignored.
 *  "O"   the argument is an LPC datatype.
 *  "s"   the argument is a string.
 *  "d"   the integer arg is printed in decimal.
 *  "i"   as d.
 *  "c"   the integer arg is to be printed as a character.
 *  "o"   the integer arg is printed in octal.
 *  "x"   the integer arg is printed in hex.
 *  "X"   the integer arg is printed in hex (in capitals).
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include "config.h"
#include "lint.h"
#include "stdio.h"
#include "interpret.h"
#include "mapping.h"
#include "object.h"
#include "sent.h"
#include "simulate.h"
#include "mstring.h"

#include "inline_svalue.h"

/*
 * If this #define is defined then error messages are returned,
 * otherwise error() is called (ie: A "wrongness in the fabric...")
 */
#undef RETURN_ERROR_MESSAGES


extern char *string_copy(char *);
static void add_indent(char **, size_t *, int);

extern struct object *current_object;

typedef unsigned int format_info;
/*
 * Format of format_info:
 *   00000000 0000xxxx : argument type:
 *				0000 : type not found yet;
 *				0001 : error type not found;
 *				0010 : percent sign, null argument;
 *				0011 : LPC datatype;
 *				0100 : string;
 *				0101 : float, engineering;
 *				0110 : float, decimal;
 *				0111 : float, shortest;
 *				1000 : integer;
 *				1001 : char;
 *				1010 : octal;
 *				1011 : hex;
 *				1100 : HEX;
 *   00000000 00xx0000 : justification:
 *				00 : right;
 *				01 : centre;
 *				10 : left;
 *   00000000 xx000000 : positive pad char:
 *				00 : none;
 *				01 : ' ';
 *				10 : '+';
 *   0000000x 00000000 : array mode?
 *   000000x0 00000000 : column mode?
 *   00000x00 00000000 : table mode?
 */

#define INFO_T 0xF
#define INFO_T_ERROR 0x1
#define INFO_T_NULL 0x2
#define INFO_T_LPC 0x3
#define INFO_T_STRING 0x4
#define	INFO_T_FLOAT_E 0x05
#define INFO_T_FLOAT_F 0x06
#define INFO_T_FLOAT_G 0x07
#define INFO_T_INT 0x8
#define INFO_T_CHAR 0x9
#define INFO_T_OCT 0xA
#define INFO_T_HEX 0xB
#define INFO_T_C_HEX 0xC

#define INFO_J 0x30
#define INFO_J_CENTRE 0x10
#define INFO_J_LEFT 0x20

#define INFO_PP 0xC0
#define INFO_PP_SPACE 0x40
#define INFO_PP_PLUS 0x80

#define INFO_ARRAY 0x100
#define INFO_COLS 0x200
#define INFO_TABLE 0x400
#define	INFO_COMMA 0x800

#define BUFF_SIZE 65535

#define ERROR(x) longjmp(error_jmp, x)
#define ERR_BUFF_OVERFLOW	0x1	/* buffer overflowed */
#define ERR_TO_FEW_ARGS		0x2	/* more arguments spec'ed than passed */
#define ERR_INVALID_STAR	0x3	/* invalid arg to * */
#define ERR_PREC_EXPECTED	0x4	/* expected precision not found */
#define ERR_INVALID_FORMAT_STR	0x5	/* error in format string */
#define ERR_INCORRECT_ARG_S	0x6	/* invalid arg to %s */
#define ERR_CST_REQUIRES_FS	0x7	/* field size not given for c/t */
#define ERR_BAD_INT_TYPE	0x8	/* bad integer type... */
#define ERR_UNDEFINED_TYPE	0x9	/* undefined type found */
#define ERR_QUOTE_EXPECTED	0xA	/* expected ' not found */
#define ERR_UNEXPECTED_EOS	0xB	/* fs terminated unexpectedly */
#define ERR_NULL_PS		0xC	/* pad string is null */
#define ERR_BAD_FLOAT_TYPE      0xD     /* Bad float type... */

#define ADD_CHAR(x) {\
  buff[bpos++] = x;\
  if (bpos > BUFF_SIZE) ERROR(ERR_BUFF_OVERFLOW); \
  curpos++;\
}

#define GET_NEXT_ARG {\
  if (++arg >= argc) ERROR(ERR_TO_FEW_ARGS); \
  carg = (argv + arg);\
}

#define SAVE_CHAR(pointer) {\
  savechars *new;\
  new = (savechars *)xalloc(sizeof(savechars));\
  new->what = *(pointer);\
  new->where = pointer;\
  new->next = saves;\
  saves = new;\
}

/*
 * list of characters to restore before exiting.
 */
typedef struct SaveChars 
{
    char what;
    char *where;
    struct SaveChars *next;
} savechars;

typedef struct ColumnSlashTable 
{
    union CSTData 
    {
	char *col;			/* column data */
	char **tab;			/* table data */
    } d;				/* d == data */
    unsigned short int nocols;	/* number of columns in table *sigh* */
    char *pad;
    unsigned int start;		/* starting cursor position */
    unsigned int size;		/* column/table width */
    int prec;			/* precision */
    format_info info;		/* formatting data */
    struct ColumnSlashTable *next;
} cst;				/* Columns Slash Tables */

static char buff[BUFF_SIZE];	/* buffer for returned string */
static unsigned int bpos;	/* position in buff */
static unsigned int curpos;	/* cursor position */
static jmp_buf error_jmp;		/* for error longjmp()s */
static struct svalue *clean;	/* Temporary storage */
static savechars *saves;	/* chars to restore */
static int call_master_ob;	/* should the master object be called for the
				 * name of an object? */
/*
 * Probably should make this a #define...
 */
static void
stradd(char **dst, size_t *size, char *add)
{
    int i;
    char *tmp;
    
    if ((i = (strlen(*dst) + strlen(add))) >= *size)
    {
	*size += i + 1;
	if (*size >= BUFF_SIZE)
	    ERROR(ERR_BUFF_OVERFLOW);
	tmp = allocate_mstring((size_t)*size);
	(void)strcpy(tmp, *dst);
	free_mstring(*dst);
	*dst = tmp;
    }
    (void)strcat(*dst, add);
} /* end of stradd() */

static void 
numadd(char **dst, size_t *size, long long snum)
{
    unsigned long long i, unum = snum;
    int nve = 0;
    char *tmp;
    int j;
    
    if (snum < 0)
    {
        unum = -snum;
	nve = 1;
    }
    for (i = 10, j = 0; unum >= i && j < 18; i *= 10, j++) /* LA XXX */
	;
    i = strlen(*dst) + nve;
    if ((i + j) >= *size)
    {
	*size += i + j + 1;
	tmp = allocate_mstring((size_t)*size);
	if (*size >= BUFF_SIZE)
	    ERROR(ERR_BUFF_OVERFLOW);
	(void)strcpy(tmp, *dst);
	free_mstring(*dst);
	*dst = tmp;
    }
    if (nve)
	(*dst)[i - 1] = '-';
    (*dst)[i + j + 1] = '\0';
    for ( ; j >= 0; j--, unum /= 10)
	(*dst)[i + j] = (unum % 10) + '0';
} /* end of num_add() */

/*
 * This is a function purely because stradd() is, to keep same param
 * passing...
 */
static void
add_indent(char **dst, size_t *size, int indent)
{
    int i;
    char *tmp;
    
    i = strlen(*dst);
    if ((i + indent) >= *size)
    {
	*size += i + indent + 1;
	if (*size >= BUFF_SIZE)
	    ERROR(ERR_BUFF_OVERFLOW);
	tmp = allocate_mstring(*size);
	(void)strcpy(tmp, *dst);
	free_mstring(*dst);
	*dst = tmp;
    }
    for ( ; indent; indent--)
	(*dst)[i++] = ' ';
    (*dst)[i] = '\0';
}

/*
 * Converts any LPC datatype into an arbitrary string format
 * and returns a pointer to this string.
 * Scary number of parameters for a recursive function.
 */
static void 
svalue_to_string(struct svalue *obj, char **str, size_t size, int indent,
		 int trailing)
{
    int i;
    if (indent >= 0)
	add_indent(str, &size, indent);
    else
	indent = -indent;
    
    switch (obj->type)
    {
    case T_INVALID:
	stradd(str,&size,"T_INVALID");
	break;
    case T_LVALUE:
	stradd(str, &size, "lvalue: ");
	svalue_to_string(obj->u.lvalue, str, size, - indent - 2, trailing);
	break;
    case T_NUMBER:
	numadd(str, &size, obj->u.number);
	break;
    case T_FLOAT:
	{
	    char buffer[1024];
	    (void)sprintf(buffer,"%.18g",obj->u.real);
	    stradd(str,&size,buffer);
	}
	break;
    case T_STRING:
	stradd(str, &size, "\"");
	stradd(str, &size, obj->u.string);
	stradd(str, &size, "\"");
	break;
    case T_POINTER:
	if (!(obj->u.vec->size))
	{
	    stradd(str, &size, "({ })");
	}
	else
	{
	    stradd(str, &size, "({ /* sizeof() == ");
	    numadd(str, &size, obj->u.vec->size);
	    stradd(str, &size, " */\n");
	    for (i = 0; i < (obj->u.vec->size) - 1; i++)
		svalue_to_string(&(obj->u.vec->item[i]), str, size,
				 indent + 2 , 1);
	    svalue_to_string(&(obj->u.vec->item[i]), str, size, indent + 2, 0);
	    stradd(str, &size, " })");
	}
	break;
    case T_OBJECT:
	if (obj->u.ob->flags & O_DESTRUCTED)
	    numadd(str, &size, 0);
	else
	{
	    struct svalue *temp;

	    stradd(str, &size, obj->u.ob->name);
	    if (call_master_ob)
	    {
		push_object(obj->u.ob);
		temp = apply_master_ob(M_OBJECT_NAME, 1);
		if (temp && (temp->type == T_STRING))
		{
		    stradd(str, &size, " (\"");
		    stradd(str, &size, temp->u.string);
		    stradd(str, &size, "\")");
		}
	    }
	}
	break;
    case T_MAPPING:
	if (!(obj->u.map->card))
	{
	    stradd(str, &size, "([ ])");
	}
	else
	{
	    struct apair *pair = 0;
	    int j;
	    stradd(str, &size, "([ /* sizeof() == ");
	    numadd(str, &size, obj->u.map->card);
	    stradd(str, &size, " */\n");
	    for (j = obj->u.map->size - 1; obj->u.map->pairs[j] == 0 && j >= 0; j--);
	    for (i = 0; i <= j; i++)
	    {
		for (pair = obj->u.map->pairs[i];
		     (i != j) ? pair : pair->next; pair = pair->next)
		{
		    svalue_to_string(&(pair->arg), str, size, indent + 2, 0);
		    stradd(str, &size, " : ");
		    svalue_to_string(&(pair->val), str, size, -indent - 2, 1);
		}
	    }
	    if (pair)
		{
		    svalue_to_string(&(pair->arg), str, size, indent + 2, 0);
		    stradd(str, &size, " : ");
		    svalue_to_string(&(pair->val), str, size, -indent - 2, 0);
		}
	    stradd(str, &size, " ])");
	}
	break;

    case T_FUNCTION:
	stradd(str, &size, "<<FUNCTION "); /* XXX function */
	stradd(str, &size, show_closure(obj->u.func));
	stradd(str, &size, ">>");
	break;

    default:
	stradd(str, &size, "!ERROR: GARBAGE SVALUE!");
    } /* end of switch (obj->type) */
    if (trailing) stradd(str, &size, ",\n");
} /* end of svalue_to_string() */

/*
 * Adds the string "str" to the buff after justifying it within "fs".
 * "trailing" is a flag which is set if trailing justification is to be done.
 * "str" is unmodified.  trailing is, of course, ignored in the case
 * of right justification.
 */
static void 
add_justified(char *str, char *pad, unsigned int fs, format_info finfo, short trailing)
{
    size_t len;
    int i;
    
    len = strlen(str);
    switch(finfo & INFO_J)
    {
    case INFO_J_LEFT:
	for (i = 0; i < len; i++)
	    ADD_CHAR(str[i]);
	fs -= len;
	len = strlen(pad);
	if (trailing)
	    for (i = 0; fs > 0; i++, fs--)
	    {
		if (pad[i%len] == '\\')
		    i++;
		ADD_CHAR(pad[i % len]);
	    }
	break;
    case INFO_J_CENTRE:
    {
	int j, l;
	
	l = strlen(pad);
	j = (fs - len) / 2 + (fs - len) % 2;
	for (i = 0; i < j; i++)
	{
	    if (len && pad[i % len] == '\\')
	    {
		i++;
		j++;
	    }
	    ADD_CHAR(pad[i % l]);
	}
	for (i = 0; i < len; i++)
	    ADD_CHAR(str[i]);
	j = (fs - len) / 2;
	if (trailing)
	    for (i = 0; i < j; i++)
	    {
		if (pad[i % l] == '\\')
		{
		    i++;
		    j++;
		}
		ADD_CHAR(pad[i % l]);
	    }
	break;
    }
    default: { /* std (s)printf defaults to right justification */
	int l;
	
	fs -= len;
	l = strlen(pad);
	for (i = 0; i < fs; i++)
	{
	    if (pad[i % l] == '\\')
	    {
		i++;
		fs++;
	    }
	    ADD_CHAR(pad[i % l]);
	}
	for (i = 0; i < len; i++)
	    ADD_CHAR(str[i]);
    }
    }
} /* end of add_justified() */
    
/*
 * Adds "column" to the buffer.
 * Returns 0 is column not finished.
 * Returns 1 if column completed.
 * Returns 2 if column completed has a \n at the end.
 */
static int 
add_column(cst **column, short int trailing)
{
    register unsigned int done;
    unsigned int save;
    
    for (done = 0;
	 (done < (*column)->prec) &&
	 ((*column)->d.col)[done] &&
	 (((*column)->d.col)[done] != '\n');
	 done++)
	;
    if (((*column)->d.col)[done] && (((*column)->d.col)[done] != '\n'))
    {
	save = done;
	for ( ; done && (((*column)->d.col)[done] != ' '); done--)
	    ;
	/*
	 * handle larger than column size words...
	 */
	if (!done)
	    done = save;
    }
    save = ((*column)->d.col)[done];
    ((*column)->d.col)[done] = '\0';
    add_justified(((*column)->d.col), (*column)->pad, (*column)->size,
		  (*column)->info,
		  (trailing || ((*column)->next)));
    ((*column)->d.col)[done] = save;
    ((*column)->d.col) += done; /* incremented below ... */
    /*
     * if this or the next character is a NULL then take this column out
     * of the list.
     */
    if (!(*((*column)->d.col)) || !(*(++((*column)->d.col))))
    {
	cst *temp;
	int ret;
	
	if (*(((*column)->d.col) - 1) == '\n')
	    ret = 2;
	else
	    ret = 1;
	temp = (*column)->next;
	free((char *)(*column));
	(*column) = temp;
	return ret;
    }
    return 0;
} /* end of add_column() */

/*
 * Adds "table" to the buffer.
 * Returns 0 if table not completed.
 * Returns 1 if table completed.
 */
static int 
add_table(cst **table, short int trailing)
{
    char save;
    register unsigned int done, i;
#define TAB (*table)
    
    for (i = 0; i < TAB->nocols && (TAB->d.tab[i]); i++)
    {
	for (done = 0; ((TAB->d.tab[i])[done]) &&
	     ((TAB->d.tab[i])[done] != '\n'); done++)
	    ;
	save = (TAB->d.tab[i])[done];
	(TAB->d.tab[i])[done] = '\0';
	add_justified((TAB->d.tab[i]), TAB->pad, TAB->size, TAB->info, 
		      (trailing || (i < TAB->nocols-1) || (TAB->next)));
	(TAB->d.tab[i])[done] = save;
	(TAB->d.tab[i]) += done; /* incremented next line ... */
	if (!(*(TAB->d.tab[i])) || !(*(++(TAB->d.tab[i]))))
	    (TAB->d.tab[i]) = 0;
    }
    if (trailing && i < TAB->nocols)
	for ( ; i < TAB->nocols; i++)
	    for (done = 0; done < TAB->size; done++)
		ADD_CHAR(' ');
    if (!TAB->d.tab[0])
    {
	cst *temp;
	
	temp = TAB->next;
	if (TAB->d.tab)
	    free((char *) TAB->d.tab);
	free((char *) TAB);
	TAB = temp;
	return 1;
    }
    return 0;
} /* end of add_table() */

void
add_commas(char *strp)
{
    char *dp;
    int n, c, i;

    for (;;)
    {
	if (*strp == '\0')
	    return;
	if (isxdigit(*strp))
	    break;
	strp++;
    }

    for (n = 0; isxdigit(*strp); n++)
	strp++;

    c = (n - 1) / 3;

    if (c > 0)
    {
	dp = strp + c;

	*dp = '\0';

	for (i = 0; i < n; i++)
	{
	    *--dp = *--strp;
	    if (i % 3 == 2 && c != 0)
	    {
		*--dp = ',';
		if (--c == 0)
		    break;
	    }
	}
    }
}

/*
 * THE (s)printf() function.
 * It returns a pointer to it's internal buffer (or a string in the text
 * segment) thus, the string must be copied if it has to survive after
 * this function is called again, or if it's going to be modified (esp.
 * if it risks being free()ed).
 */
char *
string_print_formatted(int call_master, char *format_str, int argc, struct svalue *argv)
{
    format_info finfo;
    cst *csts;		/* list of columns/tables to be done */
    struct svalue *carg;	/* current arg */
    volatile unsigned int nelemno;	/* next offset into array */
    unsigned int fpos;	/* position in format_str */
    unsigned int arg;	/* current arg number */
    unsigned int fs;	/* field size */
    int prec;		/* precision */
    unsigned int i;
    char *pad;		/* fs pad string */
    format_info format;

    nelemno = 0;
    call_master_ob = call_master;
    if ((i = setjmp(error_jmp)) != 0) { /* error handling */
	char *err = NULL;

	if (clean)
	{
	    free_svalue(clean);
	    free(clean);
	    clean = NULL;
	}
	while (saves)
	{
	    savechars *tmp;

	    *saves->where = saves->what;
	    tmp = saves;
	    saves = saves->next;
	    free(tmp);
	}
	switch(i)
	{
	case ERR_BUFF_OVERFLOW:
	    err = "BUFF_SIZE overflowed...";
	    break;
	case ERR_TO_FEW_ARGS:
	    err = "More arguments specified than passed.";
	    break;
	case ERR_INVALID_STAR:
	    err = "Incorrect argument type to *.";
	    break;
	case ERR_PREC_EXPECTED:
	    err = "Expected precision not found.";
	    break;
	case ERR_INVALID_FORMAT_STR:
	    err = "Error in format string.";
	    break;
	case ERR_INCORRECT_ARG_S:
	    err = "Incorrect argument to type %s";
	    break;
	case ERR_CST_REQUIRES_FS:
	    err = "Column/table mode requires a field size.";
	    break;
	case ERR_BAD_INT_TYPE:
	    err = "!feature - bad integer type!";
	    break;
	case ERR_UNDEFINED_TYPE:
	    err = "!feature - undefined type!";
	    break;
	case ERR_QUOTE_EXPECTED:
	    err = "Quote expected in format string.";
	    break;
	case ERR_UNEXPECTED_EOS:
	    err = "Unexpected end of format string.";
	    break;
	case ERR_NULL_PS:
	    err = "Null pad string specified.";
	    break;
        case ERR_BAD_FLOAT_TYPE:
            err = "!feature - bad float type!";
            break;
	default:
#ifdef RETURN_ERROR_MESSAGES
	    (void)sprintf(buff,
		    "ERROR: (s)printf(): !feature - undefined error 0x%X !\n", i);
	    (void)fprintf(stderr, "%s:%s: %s", current_object->name,
			  get_srccode_position_if_any(), buff);
	    return buff;
#else
	    error("ERROR: (s)printf(): !feature - undefined error 0x%X !\n", i);
#endif /* RETURN_ERROR_MESSAGES */
	}
#ifdef RETURN_ERROR_MESSAGES
	(void)sprintf(buff, "ERROR: (s)printf(): %s\n", err);
	(void)fprintf(stderr, "%s:%s: %s", current_object->name,
		      get_srccode_position_if_any(), buff);
	return buff;
#else
	error("ERROR: (s)printf(): %s\n", err);
#endif /* RETURN_ERROR_MESSAGES */
    }
    arg = (unsigned)-1;
    bpos = 0;
    curpos = 0;
    csts = 0;
    for (fpos = 0 ;; fpos++)
    {
	if ((format_str[fpos] == '\n') || (!format_str[fpos]))
	{
	    int column_stat = 0;
	    
	    if (!csts)
	    {
		if (!format_str[fpos])
		    break;
		ADD_CHAR('\n');
		curpos = 0;
		continue;
	    }
	    ADD_CHAR('\n');
	    curpos = 0;
	    while (csts)
	    {
		cst **temp;
		
		temp = &csts;
		while (*temp)
		{
		    if ((*temp)->info & INFO_COLS)
		    {
			if (*((*temp)->d.col-1) != '\n')
			    while (*((*temp)->d.col) == ' ')
				(*temp)->d.col++;
			for (i = curpos; i < (*temp)->start; i++)
			    ADD_CHAR(' ');
			column_stat = add_column(temp, 0);
			if (!column_stat)
			    temp = &((*temp)->next);
		    }
		    else
		    {
			for (i = curpos; i < (*temp)->start; i++)
			    ADD_CHAR(' ');
			if (!add_table(temp, 0))
			    temp = &((*temp)->next);
		    }
		} /* of while (*temp) */
		if (csts || format_str[fpos] == '\n')
		    ADD_CHAR('\n');
		curpos = 0;
	    } /* of while (csts) */
	    if (column_stat == 2)
		ADD_CHAR('\n');
	    if (!format_str[fpos])
		break;
	    continue;
	}
	if (format_str[fpos] == '%')
	{
	    if (format_str[fpos+1] == '%')
	    {
		ADD_CHAR('%');
		fpos++;
		continue;
	    }
	    GET_NEXT_ARG;
	    fs = 0;
	    prec = 0;
	    pad = " ";
	    finfo = 0;
	    for (fpos++; !(finfo & INFO_T); fpos++)
	    {
		if (!format_str[fpos])
		{
		    finfo |= INFO_T_ERROR;
		    break;
		}
		if (((format_str[fpos] >= '0') && (format_str[fpos] <= '9'))
		    || (format_str[fpos] == '*'))
		{
			if (prec == -1) { /* then looking for prec */
			    if (format_str[fpos] == '*')
			    {
				if (carg->type != T_NUMBER)
				    ERROR(ERR_INVALID_STAR);
				prec = carg->u.number;
				GET_NEXT_ARG;
				continue;
			    }
			    prec = format_str[fpos] - '0';
			    for (fpos++;
				 (format_str[fpos] >= '0') &&
				 (format_str[fpos] <= '9'); fpos++)
			    {
				prec = prec*10 + format_str[fpos] - '0';
			    }
			}
			else
			{ /* then is fs (and maybe prec) */
			    if ((format_str[fpos] == '0') &&
				(((format_str[fpos+1] >= '1') &&
				  (format_str[fpos+1] <= '9')) ||
				 (format_str[fpos+1] == '*')))
				pad = "0";
			    else
			    {
				if (format_str[fpos] == '*')
				{
				    if (carg->type != T_NUMBER)
					ERROR(ERR_INVALID_STAR);
				    fs = carg->u.number;
				    if (prec == -2)
					prec = fs; /* colon */
				    GET_NEXT_ARG;
				    continue;
				}
				fs = format_str[fpos] - '0';
			    }
			    for (fpos++;
				 (format_str[fpos]>='0') &&
				 (format_str[fpos]<='9'); fpos++)
			    {
				fs = fs*10 + format_str[fpos] - '0';
			    }
			    if (prec == -2)
			    { /* colon */
				prec = fs;
			    }
			}
			fpos--; /* bout to get incremented */
			continue;
		    }
		switch (format_str[fpos])
		{
		case ' ':
		    finfo |= INFO_PP_SPACE;
		    break;
		case '+': finfo |= INFO_PP_PLUS; break;
		case '-': finfo |= INFO_J_LEFT; break;
		case '|': finfo |= INFO_J_CENTRE; break;
		case '@': finfo |= INFO_ARRAY; break;
		case '=': finfo |= INFO_COLS; break;
		case '#': finfo |= INFO_TABLE; break;
		case ',': finfo |= INFO_COMMA; break;
		case '.': prec = -1; break;
		case ':': prec = -2; break;
		case '%': finfo |= INFO_T_NULL; break; /* never reached */
		case 'O': finfo |= INFO_T_LPC; break;
		case 's': finfo |= INFO_T_STRING; break;
		case 'd': finfo |= INFO_T_INT; break;
		case 'i': finfo |= INFO_T_INT; break;
		case 'c': finfo |= INFO_T_CHAR; break;
		case 'o': finfo |= INFO_T_OCT; break;
		case 'x': finfo |= INFO_T_HEX; break;
		case 'X': finfo |= INFO_T_C_HEX; break;
		case 'e': finfo |= INFO_T_FLOAT_E; break;
		case 'f': finfo |= INFO_T_FLOAT_F; break;
		case 'g': finfo |= INFO_T_FLOAT_G; break;
		case '\'':
		    pad = &(format_str[++fpos]);
		    for (;;)
		    {
			if (!format_str[fpos])
			    ERROR(ERR_UNEXPECTED_EOS);
			if (format_str[fpos] == '\\')
			{
			    fpos += 2;
			    continue;
			}
			if (format_str[fpos] == '\'')
			{
			    if (format_str+fpos == pad)
				ERROR(ERR_NULL_PS);
			    SAVE_CHAR(format_str + fpos);
			    format_str[fpos] = '\0';
			    break;
			}
			fpos++;
		    }
		    break;
		default: finfo |= INFO_T_ERROR;
		}
	    } /* end of for () */
	    if (prec < 0)
		ERROR(ERR_PREC_EXPECTED);
	    /*
	     * now handle the different arg types...
	     */
	    if (finfo & INFO_ARRAY)
	    {
		if (carg->type != T_POINTER || carg->u.vec->size == 0)
		{
		    fpos--; /* About to get incremented */
		    continue;
		}
		carg = (argv + arg)->u.vec->item;
		nelemno = 1; /* next element number */
	    }
	    for (;;)
	    {
		if ((finfo & INFO_T) == INFO_T_LPC)
		{
		    clean = (struct svalue *)xalloc(sizeof(struct svalue));
		    clean->type = T_STRING;
		    clean->string_type = STRING_MSTRING;
		    clean->u.string = allocate_mstring(512);
		    clean->u.string[0] = '\0';
		    svalue_to_string(carg, &(clean->u.string), 512, 0, 0);
		    carg = clean;
		    
		    finfo ^= INFO_T_LPC;
		    finfo |= INFO_T_STRING;
		}
		if ((finfo & INFO_T) == INFO_T_ERROR)
		{
		    ERROR(ERR_INVALID_FORMAT_STR);
		}
		else if ((finfo & INFO_T) == INFO_T_NULL)
		{
		    /* never reached... */
		    (void)fprintf(stderr, "%s: (s)printf: INFO_T_NULL.... found.\n",
				  current_object->name);
		    ADD_CHAR('%');
		} else if ((finfo & INFO_T) == INFO_T_STRING)
		{
		    int slen;
		    
		    if (carg->type != T_STRING)
			ERROR(ERR_INCORRECT_ARG_S);
		    slen = strlen(carg->u.string);
		    if ((finfo & INFO_COLS) || (finfo & INFO_TABLE))
		    {
			cst **temp;
			
			if (!fs)
			    ERROR(ERR_CST_REQUIRES_FS);
			
			temp = &csts;
			while (*temp)
			    temp = &((*temp)->next);
			if (finfo & INFO_COLS)
			{
			    *temp = (cst *)xalloc(sizeof(cst));
			    (*temp)->next = 0;
			    (*temp)->d.col = carg->u.string;
			    (*temp)->pad = pad;
			    (*temp)->size = fs;
			    (*temp)->prec = (prec) ? prec : fs;
			    (*temp)->info = finfo;
			    (*temp)->start = curpos;
			    if ((add_column(temp, (((format_str[fpos] != '\n')
						    && (format_str[fpos] != '\0')) || ((finfo & INFO_ARRAY)
										       && (nelemno < (argv+arg)->u.vec->size)))) == 2)
				&& !format_str[fpos])
			    {
				ADD_CHAR('\n');
			    }
			}
			else
			{ /* (finfo & INFO_TABLE) */
			    unsigned int n, len, max;
			    
#define TABLE carg->u.string
			    (*temp) = (cst *)xalloc(sizeof(cst));
			    (*temp)->pad = pad;
			    (*temp)->info = finfo;
			    (*temp)->start = curpos;
			    (*temp)->next = 0;
			    max = len = 0;
			    n = 1;
			    for (i = 0; TABLE[i]; i++)
			    {
				if (TABLE[i] == '\n')
				{
				    if (len > max)
					max = len;
				    len = 0;
				    if (TABLE[i + 1])
					n++;
				    continue;
				}
				len++;
			    }
			    if (prec)
			    {
				(*temp)->size = fs/prec;
			    }
			    else
			    {
				if (len > max)
				    max = len; /* the null terminated word */
				prec = fs/(max+2); /* at least two separating spaces */
				if (!prec)
				    prec = 1;
				(*temp)->size = fs / prec;
			    }
			    len = n / prec; /* length of average column */
			    if (n < prec)
				prec = n;
			    if (len * prec < n)
				len++;
			    if (len > 1 && n % prec)
				prec -= (prec - n % prec) / len;
			    (*temp)->d.tab = (char **)xalloc(prec*sizeof(char *));
			    (*temp)->nocols = prec; /* heavy sigh */
			    (*temp)->d.tab[0] = TABLE;
			    if (prec == 1)
				goto add_table_now;
			    i = 1; /* the next column number */
			    n = 0; /* the current "word" number in this column */
			    for (fs = 0; TABLE[fs]; fs++)
			    { /* throwing away fs... */
				if (TABLE[fs] == '\n')
				{
				    if (++n >= len)
				    {
					if (carg != clean)
					    SAVE_CHAR(((TABLE) + fs));
					TABLE[fs] = '\0';
					(*temp)->d.tab[i++] = TABLE+fs+1;
					if (i >= prec)
					    goto add_table_now;
					n = 0;
				    }
				}
			    }
			add_table_now:
			    (void)add_table(temp, (((format_str[fpos] != '\n') &&
						    (format_str[fpos] != '\0')) ||
						   ((finfo & INFO_ARRAY) &&
						    (nelemno < (argv + arg)->u.vec->size))));
			}
		    }
		    else
		    { /* not column or table */
			if (prec && prec < slen)
			{
			    if (carg != clean)
				SAVE_CHAR(((carg->u.string)+prec));
			    carg->u.string[prec] = '\0';
			    slen = prec;
			}
			if (fs && fs>slen) {
			    add_justified(carg->u.string, pad, fs, finfo,
					  (((format_str[fpos] != '\n') &&
					    (format_str[fpos] != '\0'))
					   || ((finfo & INFO_ARRAY) &&
					       (nelemno < (argv + arg)->u.vec->size)))
					  || carg->u.string[slen - 1] != '\n');
			}
			else
			{
			    for (i = 0; i < slen; i++)
				ADD_CHAR(carg->u.string[i]);
			}
		    }
		} else if (finfo & INFO_T_INT) { /* one of the integer types */
		    char temp[100];
		    
		    if (carg->type != T_NUMBER)
		    { /* sigh... */
#ifdef RETURN_ERROR_MESSAGES
			(void)sprintf(buff,
				      "ERROR: (s)printf(): incorrect argument type to %%d.\n");
			(void)fprintf(stderr, "%s:%s: %s", current_object->name,
				      get_srccode_position_if_any(), buff);
			return buff;
#else
			error("ERROR: (s)printf(): incorrect argument type to %%d.\n");
#endif /* RETURN_ERROR_MESSAGES */
		    }
		    format = finfo & INFO_T;
		    if (format == INFO_T_INT)
			switch (finfo & INFO_PP)
			{
			  case INFO_PP_SPACE:
			    sprintf(temp, "% lld", carg->u.number);
			    break;
			  case INFO_PP_PLUS:
			    sprintf(temp, "%+lld", carg->u.number);
			    break;
			  default:
			    sprintf(temp, "%lld", carg->u.number);
			}
		    else if (format == INFO_T_CHAR)
			sprintf(temp, "%c", (int)carg->u.number);
		    else if (format == INFO_T_OCT)
			sprintf(temp, "%llo", carg->u.number);
		    else if (format == INFO_T_HEX)
			sprintf(temp, "%llx", carg->u.number);
		    else if (format == INFO_T_C_HEX)
			sprintf(temp, "%llX", carg->u.number);
		    else {
			ERROR(ERR_BAD_INT_TYPE);
		    }
		    if (finfo & INFO_COMMA)
			add_commas(temp);
		    {
			int tmpl = strlen(temp);

			if (prec && tmpl > prec)
			    temp[prec] = '\0'; /* well.... */
			if (tmpl < fs)
			    add_justified(temp, pad, fs, finfo,
					  (((format_str[fpos] != '\n') &&
					    (format_str[fpos] != '\0')) ||
					   ((finfo & INFO_ARRAY) &&
					    (nelemno < (argv+arg)->u.vec->size))));
			else
			    for (i = 0; i < tmpl; i++)
				ADD_CHAR(temp[i]);
		    }
		}
		else if ((i=finfo&INFO_T) == INFO_T_FLOAT_E ||
			  i == INFO_T_FLOAT_F || i == INFO_T_FLOAT_G)
		{
		    char temp[100];

		    if (carg->type != T_FLOAT)
		    { /* sigh... */
#ifdef RETURN_ERROR_MESSAGES
			(void)sprintf(buff,
				"ERROR: (s)printf(): incorrect argument type to %%f.\n");
			(void)fprintf(stderr, "%s:%s: %s", current_object->name,
				      get_srccode_position_if_any(), buff);
			return buff;
#else
			error("ERROR: (s)printf(): incorrect argument type to %%f.\n");
#endif /* RETURN_ERROR_MESSAGES */
		    }

		    if (prec <= 0)
			prec = 6;
		    format = finfo & INFO_T; 
		    if (format == INFO_T_FLOAT_E)
			switch (finfo & INFO_PP) {
			  case INFO_PP_SPACE: 
			    (void)sprintf(temp,"% .*e",prec, carg->u.real);
			    break;
			  case INFO_PP_PLUS: 
			    (void)sprintf(temp,"%+.*e",prec, carg->u.real);
			    break;
			  default:
			    (void)sprintf(temp,"%.*e",prec, carg->u.real);
			    break;
			}
		    else if (format == INFO_T_FLOAT_F)
			switch (finfo & INFO_PP) {
			  case INFO_PP_SPACE: 
			    (void)sprintf(temp,"% .*f",prec, carg->u.real);
			    break;
			  case INFO_PP_PLUS: 
			    (void)sprintf(temp,"%+.*f",prec, carg->u.real);
			    break;
			  default:
			    (void)sprintf(temp,"%.*f",prec, carg->u.real);
			    break;
			}
		    else if (format == INFO_T_FLOAT_G)
			switch (finfo & INFO_PP) {
			  case INFO_PP_SPACE: 
			    (void)sprintf(temp,"% .*g",prec, carg->u.real);
			    break;
			  case INFO_PP_PLUS: 
			    (void)sprintf(temp,"%+.*g",prec, carg->u.real);
			    break;
			  default:
			    (void)sprintf(temp,"%.*g",prec, carg->u.real);
			    break;
			}
		    else {
			ERROR(ERR_BAD_FLOAT_TYPE);
		    }
		    {
			int tmpl = strlen(temp);
			
			if (tmpl < fs)
			    add_justified(temp, pad, fs, finfo,
					  (((format_str[fpos] != '\n') &&
					    (format_str[fpos] != '\0')) ||
					   ((finfo & INFO_ARRAY) &&
					    (nelemno < (argv+arg)->u.vec->size))));
			else
			    for (i = 0; i < tmpl; i++)
				ADD_CHAR(temp[i]);
		    }
		}
		else /* type not found */
		    ERROR(ERR_UNDEFINED_TYPE);

		if (clean)
		{
		    free_svalue(clean);
		    free(clean);
		    clean = 0;
		}
		if (!(finfo & INFO_ARRAY))
		    break;
		if (nelemno >= (argv+arg)->u.vec->size)
		    break;
		carg = (argv + arg)->u.vec->item + nelemno++;
	    } /* end of while (1) */
	    fpos--; /* bout to get incremented */
	    continue;
	}
	ADD_CHAR(format_str[fpos]);
    } /* end of for (fpos = 0; 1; fpos++) */
    ADD_CHAR('\0');
    while (saves)
    {
	savechars *tmp;
	*(saves->where) = saves->what;
	tmp = saves;
	saves = saves->next;
	free((char *)tmp);
    }
    return buff;
} /* end of string_print_formatted() */
