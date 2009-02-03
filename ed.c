/*
 *  ed - standard editor
 *  ~~
 *	Authors: Brian Beattie, Kees Bot, and others
 *
 * Copyright 1987 Brian Beattie Rights Reserved.
 * Permission to copy or distribute granted under the following conditions:
 * 1). No charge may be made other than reasonable charges for reproduction.
 * 2). This notice must remain intact.
 * 3). No further restrictions may be added.
 * 4). Except meaningless ones.
 *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 *  TurboC mods and cleanup 8/17/88 RAMontante.
 *  Further information (posting headers, etc.) at end of file.
 *  RE stuff replaced with Spencerian version, sundry other bugfix+speedups
 *  Ian Phillipps. Version incremented to "5".
 * _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 *  Changed the program to use more of the #define's that are at the top of
 *  this file.  Modified prntln() to print out a string instead of each
 *  individual character to make it easier to see if snooping a wizard who
 *  is in the editor.  Got rid of putcntl() because of the change to strings
 *  instead of characters, and made a define with the name of putcntl()
 *  Incremented version to "6".
 *  Scott Grosch / Belgarath    08/10/91
 * _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 *
 *  ********--->> INDENTATION ONLINE !!!! <<----------****************
 *  Indentation added by Ted Gaunt (aka Qixx) paradox@mcs.anl.gov
 *  help files added by Ted Gaunt
 *  '^' added by Ted Gaunt
 *  Note: this version compatible with v.3.0.34  (and probably all others too)
 *  but i've only tested it on v.3 please mail me if it works on your mud
 *  and if you like it!
 */

/* A quick fix that hopefully does the job! -- Buddha */
#define PROMPT ":"

int	version = 6;	/* used only in the "set" function, for i.d. */
  
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <sys/types.h>  /* need for netinet */
#include <ctype.h>
/* Regexp is Henry Spencer's package. WARNING: regsub is modified to return
 * a pointer to the \0 after the destination string, and this program refers
 * to the "private" reganch field in the struct regexp.
 */
#include "config.h"
#include "lint.h"
#include "regexp.h"
#include "interpret.h"
#include "object.h"
#include "exec.h"
#include "comm.h"
#include "comm1.h"
#include "mstring.h"
#include "mapping.h"
#include "simulate.h"

#include "inline_svalue.h"

/* define this if you don't like the ending dollar signs in ed, in n-mode */
#define NO_END_DOLLAR_SIGN
/*
 *	#defines for non-printing ASCII characters
 */
#define NUL	0x00	/* ^@ */
#define EOS	0x00	/* end of string */
#define SOH	0x01	/* ^A */
#define STX	0x02	/* ^B */
#define ETX	0x03	/* ^C */
#define EOT	0x04	/* ^D */
#define ENQ	0x05	/* ^E */
#define ACK	0x06	/* ^F */
#define BEL	0x07	/* ^G */
#define BS	0x08	/* ^H */
#define HT	0x09	/* ^I */
#define LF	0x0a	/* ^J */
#define NL	'\n'
#define VT	0x0b	/* ^K */
#define FF	0x0c	/* ^L */
#define CR	0x0d	/* ^M */
#define SO	0x0e	/* ^N */
#define SI	0x0f	/* ^O */
#define DLE	0x10	/* ^P */
#define DC1	0x11	/* ^Q */
#define DC2	0x12	/* ^R */
#define DC3	0x13	/* ^S */
#define DC4	0x14	/* ^T */
#define NAK	0x15	/* ^U */
#define SYN	0x16	/* ^V */
#define ETB	0x17	/* ^W */
#define CAN	0x18	/* ^X */
#define EM	0x19	/* ^Y */
#define SUB	0x1a	/* ^Z */
#define ESC	0x1b	/* ^[ */
#define FS	0x1c	/* ^\ */
#define GS	0x1d	/* ^] */
/*#define RS	0x1e	   ^^ */
#define US	0x1f	/* ^_ */
#define SP	0x20	/* space */
#define DEL	0x7f	/* DEL*/
#define ESCAPE  '\\'

#define TAB '\t'		/* added by Qixx for indentation */
#define LB '{'
#define RB '}'
#define LC '('
#define RC ')'
#define LS '['
#define RS ']'
#define PP '\"'
#define EOL '\0'


#define TRUE	1
#define FALSE	0
#define ERR		-2
#define FATAL		(ERR-1)
#define CHANGED		(ERR-2)
#define SET_FAIL	(ERR-3)
#define SUB_FAIL	(ERR-4)
#define MEM_FAIL	(ERR-5)
#define UNSPEC_FAIL	(ERR-6)

#define	BUFFER_SIZE	2048	/* stream-buffer size:  == 1 hd cluster */

#define LINFREE	1	/* entry not in use */
#define LGLOB	2       /* line marked global */

#define MAXLINE	512	/* max number of chars per line */
#define MAXPAT	256	/* max number of chars per replacement pattern */
#define MAXFNAME 256	/* max file name size */


/**  Global variables  **/

extern struct program *current_prog;
extern struct object *master_ob;

int EdErr = 0;

struct	line {
	int		l_stat;		/* empty, mark */
	struct line	*l_prev;
	struct line	*l_next;
	char		l_buff[1];
};
typedef struct line	LINE;

extern struct object *command_giver;
void set_prompt (char *);

#ifndef toupper
extern int toupper (int);
#endif

int doprnt (int, int);
int ins (char *);
int deflt (int, int);
int doglob (void);
int set (void);
int getlst (void);
int getone (void);
int ckglob (void);
void set_ed_buf (void);
void save_ed_buffer (void);
#ifdef ED_INDENT
static int indent_code (void);
#endif
static void print_help (int arg);
static void print_help2 (void);
static void count_blanks (int line);
static void _count_blanks (char *str, int blanks);
static void free_ed_buffer (void);

#define	ED_BUFFER	(command_giver->interactive->ed_buffer)

#define P_DIAG		(ED_BUFFER->diag)
#define P_TRUNCFLG	(ED_BUFFER->truncflg)
#define P_NONASCII	(ED_BUFFER->nonascii)
#define P_NULLCHAR	(ED_BUFFER->nullchar)
#define P_TRUNCATED	(ED_BUFFER->truncated)
#define P_FNAME		(ED_BUFFER->fname)
#define P_FCHANGED	(ED_BUFFER->fchanged)
#define P_NOFNAME	(ED_BUFFER->nofname)
#define P_MARK		(ED_BUFFER->mark)
#define P_OLDPAT	(ED_BUFFER->oldpat)
#define P_LINE0		(ED_BUFFER->Line0)
#define P_CURLN		(ED_BUFFER->CurLn)
#define P_CURPTR	(ED_BUFFER->CurPtr)
#define P_LASTLN	(ED_BUFFER->LastLn)
#define P_LINE1		(ED_BUFFER->Line1)
#define P_LINE2		(ED_BUFFER->Line2)
#define P_NLINES	(ED_BUFFER->nlines)
#define P_SHIFTWIDTH	(ED_BUFFER->shiftwidth)
#define P_FLAGS 	(ED_BUFFER->flags)
#define P_APPENDING	(ED_BUFFER->appending)
#define P_MORE		(ED_BUFFER->moring)
#define P_LEADBLANKS	(ED_BUFFER->leading_blanks)
#define P_CUR_AUTOIND   (ED_BUFFER->cur_autoindent)
/* shiftwidth is meant to be a 4-bit-value that can be packed into an int
   along with flags, therefore masks 0x1 ... 0x8 are reserved.           */
#define NFLG_MASK	0x0010
#define P_NFLG		( P_FLAGS & NFLG_MASK )
#define LFLG_MASK	0x0020
#define P_LFLG		( P_FLAGS & LFLG_MASK )
#define PFLG_MASK	0x0040
#define P_PFLG		( P_FLAGS & PFLG_MASK )
#define EIGHTBIT_MASK	0x0080
#define P_EIGHTBIT	( P_FLAGS & EIGHTBIT_MASK )
#define AUTOINDFLG_MASK	0x0100
#define P_AUTOINDFLG	( P_FLAGS & AUTOINDFLG_MASK )
#define EXCOMPAT_MASK	0x0200
#define P_EXCOMPAT	( P_FLAGS & EXCOMPAT_MASK )
#define TABINDENT_MASK	0x0400
#define P_TABINDENT	( P_FLAGS & TABINDENT_MASK )
#define SHIFTWIDTH_MASK	0x000f
#define ALL_FLAGS_MASK	0x07f0


char	inlin[MAXLINE];
char	*inptr;		/* tty input buffer */
struct ed_buffer {
	int	diag;		/* diagnostic-output? flag */
	int	truncflg;	/* truncate long line flag */
	int	nonascii;	/* count of non-ascii chars read */
	int	nullchar;	/* count of null chars read */
	int	truncated;	/* count of lines truncated */
	char	fname[MAXFNAME];
	int	fchanged;	/* file-changed? flag */
	int	nofname;
	int	mark['z'-'a'+1];
	regexp	*oldpat;
	
	LINE	Line0;
	int	CurLn;
	LINE	*CurPtr;	/* CurLn and CurPtr must be kept in step */
	int	LastLn;
	int	Line1, Line2, nlines;
	int	flags;
#if 0
	int	eightbit;	/* save eighth bit */
	int	nflg;		/* print line number flag */
	int	lflg;		/* print line in verbose mode */
	int	pflg;		/* print current line after each command */
#endif
	int	appending;
	int     moring;         /* used for the wait line of help */
	struct closure *exit_func; /* function to call on exit */
	int	shiftwidth;
	int	leading_blanks;
	int	cur_autoindent;
};

struct tbl {
	char	*t_str;
	int	t_and_mask;
	int	t_or_mask;
} *t, tbl[] = {
	{ "number",	~FALSE,		NFLG_MASK, },
	{ "nonumber",	~NFLG_MASK,	FALSE, },
	{ "list",		~FALSE,		LFLG_MASK, },
	{ "nolist",	~LFLG_MASK,	FALSE, },
	{ "print",	~FALSE, 	PFLG_MASK, },
	{ "noprint",	~PFLG_MASK,	FALSE, },
	{ "eightbit",	~FALSE,		EIGHTBIT_MASK, },
	{ "noeightbit",	~EIGHTBIT_MASK,	FALSE, },
	{ "autoindent",	~FALSE,		AUTOINDFLG_MASK, },
	{ "noautoindent",	~AUTOINDFLG_MASK, FALSE, },
	{ "excompatible", ~FALSE,		EXCOMPAT_MASK, },
	{ "noexcompatible",~EXCOMPAT_MASK,FALSE, },
	{ "tabindent",	~FALSE,		TABINDENT_MASK, },
	{ "notabindent",~TABINDENT_MASK, FALSE, },
	{ 0, 0, 0 },
};


/*-------------------------------------------------------------------------*/

extern	LINE	*getptr(int);
extern	void	prntln(char *, int, int), error(char *, ...);
regexp	*optpat(void);


/*________  Macros  ________________________________________________________*/

#ifndef max
#  define max(a,b)	((a) > (b) ? (a) : (b))
#endif

#ifndef min
#  define min(a,b)	((a) < (b) ? (a) : (b))
#endif

#define nextln(l)	((l)+1 > P_LASTLN ? 0 : (l)+1)
#define prevln(l)	((l)-1 < 0 ? P_LASTLN : (l)-1)

#define gettxtl(lin)	((lin)->l_buff)
#define gettxt(num)	(gettxtl( getptr(num) ))

#define getnextptr(p)	((p)->l_next)
#define getprevptr(p)	((p)->l_prev)

#define setCurLn( lin )	( P_CURPTR = getptr( P_CURLN = (lin) ) )
#define nextCurLn()	( P_CURLN = nextln(P_CURLN), P_CURPTR = getnextptr( P_CURPTR ) )
#define prevCurLn()	( P_CURLN = prevln(P_CURLN), P_CURPTR = getprevptr( P_CURPTR ) )

#define clrbuf()	del(1, P_LASTLN)

#define	Skip_White_Space	{while (*inptr==SP || *inptr==HT) inptr++;}

#define relink(a, x, y, b) { (x)->l_prev = (a); (y)->l_next = (b); }


/*________  functions  ______________________________________________________*/


/*	append.c	*/


int 
append(int line, int glob)
{
    if(glob)
	return(ERR);
    setCurLn( line );
    P_APPENDING = 1;
    if(P_NFLG)
	(void)add_message("%6d. ",P_CURLN+1);
    if (P_CUR_AUTOIND)
	(void)add_message("%*s",P_LEADBLANKS,"");
    set_prompt("*\b");
	return 0;
}

void
more_append(char *str)
{
    if(str[0] == '.' && str[1] == '\0')
    {
	P_APPENDING = 0;
	set_prompt(PROMPT);
	return;
    }
    if(P_NFLG)
	(void)add_message("%6d. ",P_CURLN+2);
    if ( P_CUR_AUTOIND )
    {
	int i;
	int less_indent_flag = 0;
	
	while ( *str=='\004' || *str == '\013' )
	{
	    str++;
	    P_LEADBLANKS-=P_SHIFTWIDTH;
	    if ( P_LEADBLANKS < 0 ) P_LEADBLANKS=0;
	    less_indent_flag=1;
	}
	for ( i = 0; i < P_LEADBLANKS; )
	    inlin[i++] = ' ';
	(void)strncpy(inlin + P_LEADBLANKS, str, (size_t)(MAXLINE - P_LEADBLANKS));
	inlin[MAXLINE-1] = '\0';
	_count_blanks(inlin,0);
	(void)add_message("%*s",P_LEADBLANKS,"");
	if ( !*str && less_indent_flag )
	    return;
	str = inlin;
    }
    if (strlen(str) >= MAXLINE) {
	(void)add_message("[line too long, truncated]\n");
	str[MAXLINE-1] = '\0';
    }
    (void)ins(str);
}

static void 
count_blanks(int line)
{
	_count_blanks(gettxtl(getptr(line)), 0);
}

static void 
_count_blanks(char *str, int blanks)
{
    for ( ; *str; str++ )
    {
	if ( *str == ' ' ) blanks++;
	else if ( *str == '\t' ) blanks += 8 - blanks % 8 ;
	else break;
    }
    P_LEADBLANKS = blanks < MAXLINE ? blanks : MAXLINE ;
}

/*	ckglob.c	*/

int 
ckglob()
{
    regexp	*glbpat;
    char	c, delim, *lin;
    int	num;
    LINE	*ptr;
    
    c = *inptr;
    
    if(c != 'g' && c != 'v')
	return(0);
    if (deflt(1, P_LASTLN) < 0)
	return(ERR);
    
    delim = *++inptr;
    if(delim <= ' ')
	return(ERR);
    
    glbpat = optpat();
    if(*inptr == delim)
	inptr++;
    ptr = getptr(1);
    for (num = 1; num <= P_LASTLN; num++)
    {
	ptr->l_stat &= ~LGLOB;
	if (P_LINE1 <= num && num <= P_LINE2)
	{
	    /* we might have got a NULL pointer if the
		supplied pattern was invalid		   */
	    if (glbpat)
	    {
		lin = gettxtl(ptr);
		if(regexec(glbpat, lin )) {
		    if (c == 'g')
			ptr->l_stat |= LGLOB;
		    else if (c == 'v')
			ptr->l_stat |= LGLOB;
		}
	    }
	    ptr = getnextptr(ptr);
	}
    }
    return(1);
}


/*  deflt.c
 *	Set P_LINE1 & P_LINE2 (the command-range delimiters) if the file is
 *	empty; Test whether they have valid values.
 */

int 
deflt(int def1, int def2)
{
    if(P_NLINES == 0)
    {
	P_LINE1 = def1;
	P_LINE2 = def2;
    }
    return ((P_LINE1 > P_LINE2 || P_LINE1 <= 0) ? ERR : 0);
}


/*	del.c	*/

/* One of the calls to this function tests its return value for an error
 * condition.  But del doesn't return any error value, and it isn't obvious
 * to me what errors might be detectable/reportable.  To silence a warning
 * message, I've added a constant return statement. -- RAM
 * ... It could check to<=P_LASTLN ... igp
 */

int 
del(int from, int to)
{
    LINE	*first, *last, *next, *tmp;
    
    if(from < 1)
	from = 1;
    first = getprevptr( getptr( from ) );
    last = getnextptr( getptr( to ) );
    next = first->l_next;
    while(next != last && next != &P_LINE0)
    {
	tmp = next->l_next;
	free((char *)next);
	next = tmp;
    }
    relink(first, last, first, last);
    P_LASTLN -= (to - from)+1;
    setCurLn(prevln(from));
    return(0);
}


int 
dolst(int line1, int line2)
{
    int oldflags = P_FLAGS, p;
    
    P_FLAGS |= LFLG_MASK;
    p = doprnt(line1, line2);
    P_FLAGS = oldflags;
    return p;
}

/*	doprnt.c	*/

int 
doprnt(int from, int to)
{
    from = (from < 1) ? 1 : from;
    to = (to > P_LASTLN) ? P_LASTLN : to;
    
    if(to != 0)
    {
	setCurLn( from );
	while( P_CURLN <= to )
	{
	    prntln( gettxtl( P_CURPTR ), P_LFLG, (P_NFLG ? P_CURLN : 0));
	    if( P_CURLN == to )
		break;
	    nextCurLn();
	}
    }
	return(0);
}

void prntln(char *str, int vflg, int len) 
{
    char *line, start[(MAXLINE << 1) + 2]; 

    line = start;
    if (len)
	(void)add_message("%4d ", len);
    while (*str && *str != NL)
    {
	if (*str < ' ' || *str >= DEL)
	{
	    switch (*str)
	    {
	    case '\t':
		*line++ = *str;
		break;
	    case DEL:
		*line++ = '^';
		*line++ = '?';
		break;
	    default:
		*line++ = '^';
		*line++ = (*str & 0x1F) | '@';
		break;
	    }
	}
	else
	    *line++ = *str;
	str++;
    }
#if !defined(NO_END_DOLLAR_SIGN) || defined(lint)
    if (vflg)
	*line++ = '$';
#endif
    *line = EOS;
    (void)add_message("%s\n", start);
}

/*	egets.c	*/

int 
egets(char *str, int size, FILE *stream)
{
    int	c = 0, count;
    char *cp;
    
    for (count = 0, cp = str; size > count;)
    {
	c = getc(stream);
	if(c == EOF)
	{
	    *cp = EOS;
	    if(count)
		(void)add_message("[Incomplete last line]\n");
	    return(count);
	}
	else if(c == NL)
	{
	    *cp = EOS;
	    return(++count);
	}
	else if (c == 0)
	    P_NULLCHAR++;	/* count nulls */
	else
	{
	    if(c > 127)
	    {
		if(!P_EIGHTBIT)		/* if not saving eighth bit */
		    c = c & 127;	/* strip eigth bit */
		P_NONASCII++;		/* count it */
	    }
	    *cp++ = c;	/* not null, keep it */
	    count++;
	}
    }
    str[count - 1] = EOS;
    if(c != NL)
    {
	(void)add_message("truncating line\n");
	P_TRUNCATED++;
	while((c = getc(stream)) != EOF)
	    if(c == NL)
		break;
    }
    return(count);
}  /* egets */


int 
doread(int lin, char *fname)
{
    FILE	*fp;
    int	err;
    unsigned long	bytes;
    unsigned int	lines;
    static char	str[MAXLINE];
    
    err = 0;
    P_NONASCII = P_NULLCHAR = P_TRUNCATED = 0;
    
    if (P_DIAG)
	(void)add_message("\"%s\" ",fname);
    if( (fp = fopen(fname, "r")) == NULL )
    {
	(void)add_message("'%s' isn't readable.\n", fname);
	return( ERR );
    }
    setCurLn( lin );
    for(lines = 0, bytes = 0;(err = egets(str,MAXLINE,fp)) > 0;)
    {
	bytes += err;
	if(ins(str) < 0)
	{
	    err = MEM_FAIL;
	    break;
	}
	lines++;
    }
    (void)fclose(fp);
    if(err < 0)
	return(err);
    if (P_DIAG)
    {
	(void)add_message("%u lines %lu bytes",lines,bytes);
	if(P_NONASCII)
	    (void)add_message(" [%d non-ascii]",P_NONASCII);
	if(P_NULLCHAR)
	    (void)add_message(" [%d nul]",P_NULLCHAR);
	if(P_TRUNCATED)
	    (void)add_message(" [%d lines truncated]",P_TRUNCATED);
	(void)add_message("\n");
    }
    return( err );
}  /* doread */


int 
dowrite(int from, int to, char *fname, int apflg)
{
    FILE	*fp;
    int	lin, err;
    unsigned int	lines;
    unsigned long	bytes;
    char	*str;
    LINE	*lptr;
    
    err = 0;
    lines = 0;
    bytes = 0;
    
    (void)add_message("\"%s\" ",fname);
    if((fp = fopen(fname,(apflg ? "a" : "w"))) == NULL)
    {
	(void)add_message(" can't be opened for writing!\n");
	return( ERR );
    }
    
    lptr = getptr(from);
    for(lin = from; lin <= to; lin++)
    {
	str = lptr->l_buff;
	lines++;
	bytes += strlen(str) + 1;	/* str + '\n' */
	if(fputs(str, fp) == EOF)
	{
	    (void)add_message("file write error\n");
	    err++;
	    break;
	}
	(void)fputc('\n', fp);
	lptr = lptr->l_next;
    }
    (void)add_message("%u lines %lu bytes\n",lines,bytes);
	(void)fclose(fp);
	return( err );
}  /* dowrite */


/*	find.c	*/

int find(regexp *pat, int dir)
{
    int	i, num;
    LINE	*lin;
    
    if (dir)
	nextCurLn();
    else
	prevCurLn();
    num = P_CURLN;
    lin = P_CURPTR;
    if (!pat)
	return (ERR);
    for(i = 0; i < P_LASTLN; i++ )
    {
	if(regexec( pat, gettxtl( lin ) ))
	    return(num);
	if (EdErr) { EdErr = 0; break; }
	if( dir )
	    num = nextln(num), lin = getnextptr(lin);
	else
	    num = prevln(num), lin = getprevptr(lin);
    }
    return ( ERR );
}

#if 0
/*	findg.c by Ted Gaunt for global searches....	much like 'grep' 
	especially useful when line numbering is turned on.
*/
int
findg(regexp *pat, int dir)
{
    int	i, num,count;
    LINE	*lin;
    
    count=0;
    num = P_CURLN;
    lin = P_CURPTR;
    for(i = 0; i < P_LASTLN; i++ )
    {
	if(regexec( pat, gettxtl( lin ) ))
	{
	    prntln( gettxtl( lin ), P_LFLG, (P_NFLG ? P_CURLN : 0));
	    count++;
	}
	if( dir )
	    num = nextln(num), lin = getnextptr(lin);
	else
	    num = prevln(num), lin = getprevptr(lin);
	nextCurLn();
    }
    if (!count)
	return ( ERR );
    else return (count);
}
#endif /* 0 */

/*	getfn.c	*/

char *
getfn(int writeflg)
{
    static char	file[MAXFNAME];
    char	*cp;
    char *file2;
    struct svalue *ret;

    
    if (*inptr == NL)
    {
	P_NOFNAME=TRUE;
	file[0] = '/';
	(void)strcpy(file+1, P_FNAME);
    }
    else
    {
	P_NOFNAME=FALSE;
	Skip_White_Space;
	
	cp = file;
	while(*inptr && *inptr != NL && *inptr != SP && *inptr != HT)
	    *cp++ = *inptr++;
	*cp = '\0';
	
    }
    if(*file == '\0')
    {
	(void)add_message("Bad file name.\n");
	return( NULL );
    }

    if (file[0] != '/')
    {
	push_string(file, STRING_MSTRING);
	ret = apply_master_ob(M_MAKE_PATH_ABSOLUTE, 1);
	if (ret == 0 || ret->type != T_STRING)
	    return NULL;
	(void)strncpy(file, ret->u.string, sizeof file - 1);
    }
    file2 = check_valid_path(file, command_giver,
			     "ed_start", writeflg);

    if (!file2)
	return( NULL );
    (void)strncpy(file, file2, MAXFNAME-1);
    file[MAXFNAME-1] = 0;

    if(*file == '\0') {
    	(void)add_message("no file name\n");
    	return(NULL);
    }
    return( file );
}  /* getfn */


int 
getnum(int first)
{
    regexp	*srchpat;
    int	num;
    char	c;
    
    Skip_White_Space;
    
    if(*inptr >= '0' && *inptr <= '9')
    {	/* line number */
	for(num = 0; *inptr >= '0' && *inptr <= '9'; ++inptr) {
	    num = (num * 10) + (*inptr - '0');
	}
	return num;
    }
    
    switch(c = *inptr)
    {
    case '.':
	inptr++;
	return (P_CURLN);
	
    case '$':
	inptr++;
	return (P_LASTLN);
	
    case '/':
    case '?':
	srchpat = optpat();
	if(*inptr == c)
	    inptr++;
	return(find(srchpat,c == '/'?1:0));
	
#if 0
    case '^':			/* for grep-like searching */
    case '&':
	srchpat = optpat();
	if(*inptr == c)
	    inptr++;
	return(findg(srchpat, c == '^' ? 1 : 0));
#endif
	
    case '-':
    case '+':
	return(first ? P_CURLN : 1);
	
    case '\'':
	inptr++;
	if (*inptr < 'a' || *inptr > 'z')
	    return(EOF);
	return P_MARK[ *inptr++ - 'a' ];
	
    default:
	return ( first ? EOF : 1 );	/* unknown address */
    }
}  /* getnum */


/*  getone.c
 *	Parse a number (or arithmetic expression) off the command line.
 */
#define FIRST 1
#define NOTFIRST 0

int 
getone()
{
    int	c, i, num;
    
    if((num = getnum(FIRST)) >= 0)
    {
	for (;;)
	{
	    Skip_White_Space;
	    if(*inptr != '+' && *inptr != '-')
		break;	/* exit infinite loop */
	    
	    c = *inptr++;
	    if((i = getnum(NOTFIRST)) < 0)
		return ( i );
	    if(c == '+')
		num += i;
	    else
		num -= i;
	}
    }
    return ( num>P_LASTLN ? ERR : num );
}  /* getone */

int
getlst()
{
    int	num;
    
    P_LINE2 = 0;
    for(P_NLINES = 0; (num = getone()) >= 0;)
    {
	P_LINE1 = P_LINE2;
	P_LINE2 = num;
	P_NLINES++;
	if(*inptr != ',' && *inptr != ';')
	    break;
	if(*inptr == ';')
	    setCurLn( num );
	inptr++;
    }
    P_NLINES = min(P_NLINES, 2);
    if(P_NLINES == 0)
	P_LINE2 = P_CURLN;
    if(P_NLINES <= 1)
	P_LINE1 = P_LINE2;
    
    return ( (num == ERR) ? num : P_NLINES );
}  /* getlst */


/*	getptr.c	*/

LINE *getptr(num)
int	num;
{
	LINE	*ptr;
	int	j;

	if (2*num>P_LASTLN && num<=P_LASTLN) {	/* high line numbers */
		ptr = P_LINE0.l_prev;
		for (j = P_LASTLN; j>num; j--)
			ptr = ptr->l_prev;
	} else {				/* low line numbers */
		ptr = &P_LINE0;
		for(j = 0; j < num; j++)
			ptr = ptr->l_next;
	}
	return(ptr);
}


/*	getrhs.c	*/

int 
getrhs(char *sub)
{
    char delim = *inptr++;
    char *outmax = sub + MAXPAT;
    if( delim == NL || *inptr == NL)	/* check for eol */
	return( ERR );
    while( *inptr != delim && *inptr != NL )
    {
	if ( sub > outmax )
	    return ERR;
	if ( *inptr == ESCAPE )
	{
	    switch ( *++inptr )
	    {
	    case 'r':
		*sub++ = '\r';
		inptr++;
		break;
#if 0
	    case ESCAPE:
		*sub++ = ESCAPE;
		*sub++ = ESCAPE;
		inptr++;
#endif
	    case 'n':
		*sub++ = '\n';
		inptr++;
		break;
	    case 'b':
		*sub++ = '\b';
		inptr++;
		break;
	    case 't':
		*sub++ = '\t';
		inptr++;
		break;
	    case '0': {
		int i=3;
		*sub = 0;
		do
		{
		    if (*++inptr<'0' || *inptr >'7')
			break;
		    *sub = (*sub<<3) | (*inptr-'0');
		} while (--i != 0);
		sub++;
	    }
	    break;
#if 0
	    default:
		if ( *inptr != delim )
		    *sub++ = ESCAPE;
#else
	    case '&':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
	    case '\\':
		*sub++ = ESCAPE;
		/* FALLTHROUGH */
	    default:
#endif
		*sub++ = *inptr;
		if ( *inptr != NL )
		    inptr++;
	    }
	}
	else *sub++ = *inptr++;
    }
    *sub = '\0';
    
    inptr++;		/* skip over delimter */
    Skip_White_Space;
    if(*inptr == 'g')
    {
	inptr++;
	return 1;
    }
    return 0;
}

/*	ins.c	*/

int ins(char *str)
{
    char	*cp;
    LINE	*new, *nxt;
    size_t	len;
    
    do
    {
	for (cp = str; *cp && *cp != NL; cp++)
	    ;
	len = cp - str;
	/* cp now points to end of first or only line */
	
	if((new = (LINE *) xalloc(sizeof(LINE) + len)) == NULL)
	    return( MEM_FAIL ); 	/* no memory */
	
	new->l_stat=0;
	(void)strncpy(new->l_buff, str, len);	/* build new line */
	new->l_buff[len] = EOS;
	nxt = getnextptr(P_CURPTR);	/* get next line */
	relink(P_CURPTR, new, new, nxt);	/* add to linked list */
	relink(new, nxt, P_CURPTR, new);
	P_LASTLN++;
	P_CURLN++;
	P_CURPTR = new;
	str = cp + 1;
    }
    while( *cp != EOS );
	return 1;
}


/*	join.c	*/

int 
join(int first, int last)
{
    char buf[MAXLINE];
    char *cp = buf, *str;
    LINE *lin;
    int num, flag;
    
    if (first <= 0 || first > last || last > P_LASTLN)
	return(ERR);
    if (first == last)
    {
	setCurLn( first );
	return 0;
    }
    lin = getptr(first);
    for (flag = 1, num = first; num <= last; num++)
    {
	str = gettxtl(lin);
	while (*str)
	{
	    if (flag || (*str != TAB && *str != SP))
	    {
		if (!flag)
		    *cp++ = SP;
		if (cp >= buf + MAXLINE - 1 )
		{
		    (void)add_message("line too long\n");
		    return(ERR);
		}
		*cp++ = *str;
		flag = 1;
	    }
	    str++;
	}
	flag = 0;
	lin = getnextptr(lin);
    }
    *cp = EOS;
    (void)del(first, last);
    if( ins(buf) < 0 )
	return MEM_FAIL;
    P_FCHANGED = TRUE;
    return 0;
}


/*  move.c
 *	Unlink the block of lines from P_LINE1 to P_LINE2, and relink them
 *	after line "num".
 */

int
moveblock(int num)
{
    int	range;
    LINE	*before, *first, *last, *after;
    
    if( P_LINE1 <= num && num <= P_LINE2 )
	return( ERR );
    range = P_LINE2 - P_LINE1 + 1;
    before = getptr(prevln(P_LINE1));
    first = getptr(P_LINE1);
    last = getptr(P_LINE2);
    after = getptr(nextln(P_LINE2));
    
    relink(before, after, before, after);
    P_LASTLN -= range;	/* per ASTs posted patch 2/2/88 */
    if (num > P_LINE1)
	num -= range;
    
    before = getptr(num);
    after = getptr(nextln(num));
    relink(before, first, last, after);
    relink(last, after, before, first);
    P_LASTLN += range;	/* per ASTs posted patch 2/2/88 */
    setCurLn( num + range );
    return 1;
}


int
transfer(int num)
{
    int mid, lin, ntrans;
    
    if (P_LINE1 <= 0 || P_LINE1 > P_LINE2)
	return(ERR);
    
    mid = num < P_LINE2 ? num : P_LINE2;
    
    setCurLn( num );
    ntrans = 0;
    
    for (lin = P_LINE1; lin <= mid; lin++)
    {
	if( ins(gettxt(lin)) < 0 )
	    return MEM_FAIL;
	ntrans++;
    }
    lin+=ntrans;
    P_LINE2+=ntrans;
    
    for ( ; lin <= P_LINE2; lin += 2 )
    {
	if( ins(gettxt(lin)) < 0 )
	    return MEM_FAIL;
	P_LINE2++;
    }
    return(1);
}


/*	optpat.c	*/

regexp *
optpat()
{
    char	delim, str[MAXPAT], *cp;
    
    delim = *inptr++;
    if (delim == NL)
	return P_OLDPAT;
    cp = str;
    while(*inptr != delim && *inptr != NL && *inptr != EOS &&
	  cp < str + MAXPAT - 1)
    {
	if(*inptr == ESCAPE && inptr[1] != NL)
	    *cp++ = *inptr++;
	*cp++ = *inptr++;
    }
    
    *cp = EOS;
    if(*str == EOS)
	return(P_OLDPAT);
    if(P_OLDPAT)
	free((char *) P_OLDPAT);
    return P_OLDPAT = regcomp(str, P_EXCOMPAT);
}

/* regerror.c */
void 
regerror(char *s)
{
	(void)add_message("ed: %s\n", s );
}

int 
set()
{
    char	word[16];
    int	i;
    
    if(*(++inptr) != 't') {
	if(*inptr != SP && *inptr != HT && *inptr != NL)
	    return(ERR);
    } else
	inptr++;
    
    if ( (*inptr == NL))
    {
	(void)add_message("ed version %d.%d\n", version/100, version%100);
	for(t = tbl; t->t_str; t+=2) {
	    (void)add_message("%s:%s ", t->t_str, 
			      P_FLAGS & t->t_or_mask ?"on":"off");
	}
	(void)add_message("\nshiftwidth:%d\n",P_SHIFTWIDTH);
	return(0);
    }
    
    Skip_White_Space;
    for(i = 0; *inptr != SP && *inptr != HT && *inptr != NL;)
    {
	if (i == sizeof word - 1) {
	    (void)add_message("Too long argument to 'set'!\n");
	    return 0;
	}
	word[i++] = *inptr++;
    }
    word[i] = EOS;
    for(t = tbl; t->t_str; t++)
    {
	if(strcmp(word,t->t_str) == 0)
	{
	    P_FLAGS = (P_FLAGS & t->t_and_mask) | t->t_or_mask;
	    return(0);
	}
	
    }
    if (strcmp(word, "save") == 0)
    {
	struct svalue *ret;
	push_object(command_giver);
	/*		push_object(command_giver, "ed: set()" ); */
	push_number( P_SHIFTWIDTH | P_FLAGS );
	ret = apply_master_ob(M_SAVE_ED_SETUP,2);
	if ( ret && ret->type == T_NUMBER && ret->u.number > 0 )
	    return 0;
    }
    if (strcmp(word, "shiftwidth") == 0)
    {
	Skip_White_Space;
	if ( isdigit(*inptr) )
	{
	    P_SHIFTWIDTH = *inptr-'0';
	    return 0;
	}
    }
    return SET_FAIL;
}

#ifndef relink
void
relink(LINE *a, LINE *x, LINE *y, LINE *b)
{
    x->l_prev = a;
    y->l_next = b;
}
#endif

void 
set_ed_buf()
{
    relink(&P_LINE0, &P_LINE0, &P_LINE0, &P_LINE0);
    P_CURLN = P_LASTLN = 0;
    P_CURPTR = &P_LINE0;
}


/*	subst.c	*/

int
subst(regexp *pat, char *sub, int gflg, int pflag)
{
    int	nchngd = 0;
    char	*txtptr;
    char	*new, *old, buf[MAXLINE];
    int	space;			/* amylaar */
    int	still_running = 1;
    LINE	*lastline = getptr( P_LINE2 );
    
    if(P_LINE1 <= 0)
	return( SUB_FAIL );
    nchngd = 0;		/* reset count of lines changed */
    
    for( setCurLn( prevln( P_LINE1 ) ); still_running; )
    {
	nextCurLn();
	new = buf;
	space = MAXLINE;	/* amylaar */
	if ( P_CURPTR == lastline )
	    still_running = 0;
	if ( regexec( pat, txtptr = gettxtl( P_CURPTR ) ) )
	{
	    do
	    {
		/* Copy leading text */
		size_t diff = pat->startp[0] - txtptr;

		if ( (space-=diff) < 0 )	/* amylaar */
		    return SUB_FAIL;
		(void)strncpy( new, txtptr, diff );
		new += diff;
		/* Do substitution */
		old = new;
		new = regsub( pat, sub, new, space);
		if (!new || (space-= new-old) < 0) /* amylaar */
		    return SUB_FAIL;
		if (txtptr == pat->endp[0])
		{ /* amylaar :
		    prevent infinite loop */
		    if ( !*txtptr ) break;
		    if (--space < 0) return SUB_FAIL;
		    *new++ = *txtptr++;
		}
		else
		    txtptr = pat->endp[0];
	    }
	    while( gflg && !pat->reganch && regexec( pat, txtptr ));
	    
	    /* Copy trailing chars */
	    /* amylaar : always check for enough space left
		* BEFORE altering memory
		    */
	    if ( (space -= strlen(txtptr) + 1 ) < 0 )
		return SUB_FAIL;
	    (void)strcpy(new, txtptr);
	    (void)del(P_CURLN, P_CURLN);
	    if( ins(buf) < 0 )
		return MEM_FAIL;
	    nchngd++;
	    if(pflag)
		(void)doprnt(P_CURLN, P_CURLN);
	}
    }
    return (( nchngd == 0 && !gflg ) ? SUB_FAIL : nchngd);
}

#ifdef ED_INDENT
/*
 * Indent code, adapted from DGD ed.
 */
static int lineno, errs;
static int shi;
static int full_shift, small_shift;

static void
shift(char *text)
{
    int idx;

    /*
     * First determine the number of leading spaces
     */
    idx = 0;
    while (*text == ' ' || *text == '\t')
    {
	if (*text++ == ' ')
	    idx++;
	else
	    idx = (idx + 8) & ~7;
    }

    /*
     * Don't shift lines consisting of only whitespace
     */
    if (*text != '\0')
    {
	idx += shi;
	if (idx < MAXLINE)
	{
	    char buffer[MAXLINE];
	    char *p;

	    p = buffer;
	    /*
	     * Fill with leading whitespace
	     */
	    if (P_TABINDENT)
		while (idx >= 8)
		{
		    *p++ = '\t';
		    idx -= 8;
		}
	    while (idx > 0)
	    {
		*p++ = ' ';
		idx--;
	    }
	    if (p - buffer + strlen(text) < MAXLINE)
	    {
		(void)strcpy(p, text);
		(void)del(lineno, lineno);
		(void)ins(buffer);
		return;
	    }
	}
	(void)add_message("Result of shift would be too long, line %d\n", lineno);
	errs++;
    }
}

#define	STACKSZ		1024	/* depth of indent stack */

/*
 * Token definitions in indent
 */
#define	SEMICOLON	0
#define	LBRACKET	1
#define	RBRACKET	2
#define	LOPERATOR	3
#define	ROPERATOR	4
#define	LHOOK		5
#define	LHOOK2		6
#define	RHOOK		7
#define	TOKEN		8
#define	ELSE		9
#define	IF		10
#define	FOR		11
#define	WHILE		12
#define	DO		13
#define	XEOT		14

static struct pps_stack {
    char *stack, *stackbot;
    int *ind, *indbot;
    struct pps_stack *last;
} *pps;

static char *stack, *stackbot;				/* token stack */
static int *ind, *indbot;				/* indent stack */
static char quote;					/* ' or " */
static int in_ppcontrol, in_comment, after_keyword;	/* status */

static void
indent(char *buf)
{
/*                      ;  {  }  (  )  [  ([ ]  tok el if fo whi do xe   */
/*                                     (  ({ )  en  se    r  le     ot   */
    static char f[] = { 7, 1, 7, 1, 2, 1, 1, 6, 4,  2, 6, 7, 7,  2, 0, };
    static char g[] = { 2, 2, 1, 7, 1, 5, 5, 1, 3,  6, 2, 2, 2,  2, 0, };
    char text[MAXLINE], ident[MAXLINE];
    char *p, *isp;
    int *ip;
    long indent_index;
    int top, token;
    char *start;
    int do_indent;

    /*
     * Problem: in this editor memory for deleted lines is reclaimed. So
     * we cannot shift the line and then continue processing it, as in
     * DGD ed. Instead make a copy of the line, and process the copy.
     * Dworkin 920510
     */
    (void)strcpy(text, buf);

    do_indent = FALSE;
    indent_index = 0;
    p = text;

    /*
     * process status vars
     */
    if (quote != '\0')
	shi = 0;	/* in case a comment starts on this line */
    else if ((in_ppcontrol || *p == '#') && p[1] != '\'')
    {
	if (*p == '#') 
	{
	    /*
	     * Check for #if, #else or #endif pps's,
	     * first skip whitespace. */
	    for (start = p; isspace(*++start); )
		;
	    if (strncmp(start, "if", 2) == 0)
	    {
		struct pps_stack *nx;
		int ssz, isz;

		/* push current status on the pps stack */
		nx = (struct pps_stack *)xalloc(sizeof(struct pps_stack));
		nx->stack = stack;
		nx->stackbot = stackbot;
		nx->ind = ind;
		nx->indbot = indbot;
		nx->last = pps;
		pps = nx;
		ssz = stack - stackbot;
		isz = ind - indbot;
		stackbot = (char *)xalloc(sizeof(char) * STACKSZ);
		stack = stackbot + ssz;
		indbot = (int *)xalloc(sizeof(int) * STACKSZ);
		ind = indbot + isz;
		(void)memcpy(stack, nx->stack, (size_t)(STACKSZ - ssz));
		(void)memcpy(ind, nx->ind, sizeof(int) * (STACKSZ - isz));
	    }
	    else if (strncmp(start, "else", 4) == 0)
	    {
		struct pps_stack *nx;

		/*
		 * take current status off the top of the pps stack,
		 * but don't free the stack. That's done by the #endif
		 * Notify invalidness of stack by zeroing a pointer
		 */
		nx = pps;
		if (!nx || !nx->stackbot)
		{
		    (void)add_message("#else without #if in line %d\n", lineno);
		    errs++;
		    return;
		}
		free(stackbot);
		free(indbot);
		stack = nx->stack;
		stackbot = nx->stackbot;
		ind = nx->ind;
		indbot = nx->indbot;
		nx->stackbot = 0;
		nx->indbot = 0;
	    }
	    else if (strncmp(start, "endif", 5) == 0)
	    {
		struct pps_stack *nx;

		/*
		 * Dump top status off the stack
		 */
		nx = pps;
		if (!nx)
		{
		    (void)add_message("#endif without #if in line %d\n", lineno);
		    errs++;
		    return;
		}
		free(nx->stackbot);
		free(nx->indbot);
		pps = nx->last;
		free(nx);
	    }
	}
	while (*p != '\0')
	{
	    if (*p == '\\' && *++p == '\0')
	    {
		in_ppcontrol = TRUE;
		return;
	    }
	    p++;
	}
	in_ppcontrol = FALSE;
	return;
    }
    else
    {
	/*
	 * count leading whitespace
	 */
	while (*p == ' ' || *p == '\t')
	{
	    if (*p++ == ' ')
		indent_index++;
	    else
		indent_index = (indent_index + 8) & ~7;
	}
	if (*p == '\0')
	{
	    (void)del(lineno, lineno);
	    (void)ins(p);
	    return;
	}
	else if (in_comment)
	    shift(text);	/* use previous shi */
	else
	    do_indent = TRUE;
    }

    /*
     * process this line
     */
    start = p;
    while (*p != '\0')
    {
	/*
	 * lexical scanning: find the next token
	 */
	ident[0] = '\0';
	if (in_comment)
	{
	    /*
	     * comment
	     */
	    while (*p != '*')
	    {
		if (*p == '\0')
		    return;
		p++;
	    }
	    while (*p == '*')
		p++;
	    if (*p == '/')
	    {
		in_comment = FALSE;
		p++;
	    }
	    continue;
	}
	else if (quote != '\0')
	{
	    /*
	     * string or character constant
	     */
	    for (;;)
	    {
		if (*p == quote)
		{
		    quote = '\0';
		    p++;
		    break;
		}
		else if (*p == '\0')
		{
		    (void)add_message("Unterninated string on line %d\n", lineno);
		    errs++;
		    return;
		}
		else if (*p == '\\' && *++p == '\0')
		    break;
		p++;
	    }
	    token = TOKEN;

	}
	else
	{
	    switch (*p++)
	    {
		case ' ':	/* white space */
		case '\t':
		    continue;

		case '\'':
		    if ((isalnum(*p) || *p == '_') && p[1] && p[1] != '\'')
		    {
			do
			    ++p;
			while (isalnum(*p) || *p == '_');
			token = TOKEN;
			break;
		    }
		    if (*p == '(' && p[1] == '{')
		    {
			/*
			 * treat quoted array like an array
			 */
			token = TOKEN;
			break;
		    }
		    /* FALLTHROUGH */
		case '"':	/* start of string */
		    quote = p[-1];
		    continue;

		case '/':
		    if (*p == '*')	/* start of comment */
		    {
			in_comment = TRUE;
			if (do_indent)
			{
			    /* this line hasn't been indented yet */
			    shi = (int)(*ind - indent_index);
			    shift(text);
			    do_indent = FALSE;
			}
			else
			{
			    register char *q;
			    register int index2;

			    /*
			     * find how much the comment has shifted, so the
			     * same shift can be used if the coment continues
			     * on the next line
			     */
			    index2 = *ind;
			    for (q = start; q < p - 1;)
			    {
				if (*q++ == '\t')
				{
				    indent_index = (indent_index + 8) & ~7;
				    index2 = (index2 + 8) & ~7;
				}
				else
				{
				    indent_index++;
				    index2++;
				}
			    }
			    shi = (int)(index2 - indent_index);
			}
			p++;
			continue;
		    }
		    if (*p == '/')	/* start of C++ style comment */
			p = strchr(p, '\0');
		    token = TOKEN;
		    break;

		case '{':
		    token = LBRACKET;
		    break;

		case '(':
		    if (after_keyword)
		    {
			/*
			 * LOPERATOR & ROPERATOR are a kludge. The operator
			 * precedence parser that is used could not work if
			 * parenthesis after keywords was not treated
			 * specially.
			 */
			token = LOPERATOR;
			break;
		    }
		    if (*p == '{' || *p == '[')
		    {
			p++;	/* ({ , ([ each are one token */
			token = LHOOK2;
			break;
		    }
		    /* FALLTHROUGH */
		case '[':
		    token = LHOOK;
		    break;

		case '}':
		    if (*p != ')')
		    {
			token = RBRACKET;
			break;
		    }
		    /* }) is one token */
		    p++;
		    token = RHOOK;
		    break;
		case ']':
		    if (*p == ')' &&
			(*stack == LHOOK2 ||
			 (*stack != XEOT &&
			  (stack[1] == LHOOK2 ||
			   (stack[1] == ROPERATOR && stack[2] == LHOOK2)))))
			p++;
		    /* FALLTHROUGH */
		case ')':
		    token = RHOOK;
		    break;

		case ';':
		    token = SEMICOLON;
		    break;

		default:
		    if (isalpha(*--p) || *p == '_')
		    {
			register char *q;

			/*
			 * Identifier. See if it's a keyword.
			 */
			q = ident;
			do
			    *q++ = *p++;
			while (isalnum(*p) || *p == '_');
			*q = '\0';

			if      (strcmp(ident, "if"   ) == 0)	token = IF;
			else if (strcmp(ident, "else" ) == 0)	token = ELSE;
			else if (strcmp(ident, "for"  ) == 0)	token = FOR;
			else if (strcmp(ident, "while") == 0)	token = WHILE;
			else if (strcmp(ident, "do"   ) == 0)	token = DO;
			else    /* not a keyword */		token = TOKEN;
		    }
		    else
		    {
			/*
			 * anything else is a "token"
			 */
			p++;
			token = TOKEN;
		    }
		    break;
	    }
	}

	/* parse */
	isp = stack;
	ip = ind;
	for (;;)
	{
	    top = *isp;
	    /* ) after LOPERATOR is ROPERATOR */
	    if (top == LOPERATOR && token == RHOOK)
		token = ROPERATOR;

	    if (f[top] <= g[token])	/* shift the token on the stack */
	    {
		register int i;

		if (isp == stackbot)
		{
		    /*
		     * out of stack
		     */
		    error("Nesting too deep in line\n");
		}

		/*
		 * handle indentation
		 */
		i = *ip;
		/*
		 * if needed, reduce indentation prior to shift
		 */
		if ((token == LBRACKET &&
		     (*isp == ROPERATOR || *isp == ELSE || *isp == DO)) ||
		    token == RBRACKET ||
		    (token == IF && *isp == ELSE))
		{
		    /*
		     * back up
		     */
		    i -= full_shift;
		}
		else if (token == RHOOK || token == ROPERATOR)
		    i -= small_shift;

		/*
		 * shift the current line, if appropriate
		 */
		if (do_indent)
		{
		    shi = (int)(i - indent_index);
		    /*
		     * back up if this is a switch label
		     */
		    if (token == TOKEN && *isp == LBRACKET &&
			(strcmp(ident, "case") == 0 ||
			 strcmp(ident, "default") == 0))
			shi -= full_shift;
		    shift(text);
		    do_indent = FALSE;
		}
		/*
		 * change indentation after current token
		 */
		switch (token)
		{
		    case LBRACKET:
		    case ROPERATOR:
		    case ELSE:
		    case DO:
			/*
			 * add indentation
			 */
			i += full_shift;
			break;
		    case LOPERATOR:
		    case LHOOK:
		    case LHOOK2:
			/*
			 * half indent after ( [ ({ ([
			 */
			i += small_shift;
			break;
		    case SEMICOLON:
			/*
			 * in case it is followed by a comment
			 */
			if (*isp == ROPERATOR || *isp == ELSE)
			    i -= full_shift;
			break;
		}

		*--isp = token;
		*--ip = i;
		break;
	    }

	    /*
	     * reduce handle
	     */
	    do
	    {
		top = *isp++;
		ip++;
	    }
	    while (f[(int)*isp] >= g[top]);
	}
	stack = isp;
	ind = ip;
	after_keyword = (token >= IF);	/* but not after ELSE */
    }
}

static void
clean_pps_stack(void)
{
    struct pps_stack *nx;
    while ((nx = pps) != NULL)
    {
	pps = nx->last;
	free(nx->stackbot);
	free(nx->indbot);
	free(nx);
    }
}

static int 
indent_code()
{
    /*
     * setup stacks
     */
    stackbot = (char *)xalloc(sizeof(char) * STACKSZ);
    indbot = (int *)xalloc(sizeof(int) * STACKSZ);
    stack = stackbot + STACKSZ - 1;
    *stack = XEOT;
    ind = indbot + STACKSZ - 1;
    *ind = 0;
    pps = 0;

    quote = '\0';
    in_ppcontrol = FALSE;
    in_comment = FALSE;

    P_FCHANGED = TRUE;
    errs = 0;
    full_shift = P_SHIFTWIDTH;
    small_shift = full_shift / 2;

    P_FCHANGED = TRUE;
    for (lineno = 1; lineno <= P_LASTLN; lineno++) 
    {
	setCurLn(lineno);
	indent(gettxtl(P_CURPTR));
	if (errs != 0)
	{
	    clean_pps_stack();
	    return ERR;
    }
    }
    clean_pps_stack();
    return 0;
}
#endif

/*  docmd.c
 *	Perform the command specified in the input buffer, as pointed to
 *	by inptr.  Actually, this finds the command letter first.
 */

int 
docmd(int glob)
{
    static char	rhs[MAXPAT];
    regexp	*subpat;
    int	c, err, line3;
    int	apflg, pflag, gflag;
    int	nchng;
    char	*fptr;
    
    pflag = FALSE;
    Skip_White_Space;
    
    c = *inptr++;
    switch(c) {
    case NL:
	if( P_NLINES == 0 && (P_LINE2 = nextln(P_CURLN)) == 0 )
	    return(ERR);
	setCurLn( P_LINE2 );
	return (1);
	
    case '=':
	(void)add_message("%d\n",P_LINE2);
	break;
	
    case 'o':
    case 'a':
    case 'A':
	P_CUR_AUTOIND = c=='a' ? P_AUTOINDFLG : !P_AUTOINDFLG;
	if(*inptr != NL || P_NLINES > 1)
	    return(ERR);
	
	if ( P_CUR_AUTOIND ) count_blanks(P_LINE1);
	if(append(P_LINE1, glob) < 0)
	    return(ERR);
	P_FCHANGED = TRUE;
	break;
	
    case 'c':
	if(*inptr != NL)
	    return(ERR);
	
	if(deflt(P_CURLN, P_CURLN) < 0)
	    return(ERR);
	
	P_CUR_AUTOIND = P_AUTOINDFLG;
	if ( P_AUTOINDFLG ) count_blanks(P_LINE1);
	if(del(P_LINE1, P_LINE2) < 0)
	    return(ERR);
	if(append(P_CURLN, glob) < 0)
	    return(ERR);
	P_FCHANGED = TRUE;
	break;
	
    case 'd':
	if (*inptr == 'p') {
	    inptr++;
	    pflag++;
	}

	if(*inptr != NL)
	    return(ERR);
	
	if(deflt(P_CURLN, P_CURLN) < 0)
	    return(ERR);
	
	if(del(P_LINE1, P_LINE2) < 0)
	    return(ERR);
	if(nextln(P_CURLN) != 0)
	    nextCurLn();
	P_FCHANGED = TRUE;
	if (pflag && doprnt(P_CURLN, P_CURLN) < 0)
	    return(ERR);
	break;
	
    case 'e':
	if(P_NLINES > 0)
	    return(ERR);
	if(P_FCHANGED)
	    return CHANGED;
	/* FALLTHROUGH */
    case 'E':
	if(P_NLINES > 0)
	    return(ERR);
	
	if(*inptr != ' ' && *inptr != HT && *inptr != NL)
	    return(ERR);
	
	if((fptr = getfn(0)) == NULL)
	    return(ERR);
	
	(void)clrbuf();
	(void)doread(0, fptr);
	
	(void)strcpy(P_FNAME, fptr);
	P_FCHANGED = FALSE;
	break;
	
    case 'f':
	if(P_NLINES > 0)
	    return(ERR);
	
	if(*inptr != ' ' && *inptr != HT && *inptr != NL)
	    return(ERR);
	
	fptr = getfn(0);
	
	if (P_NOFNAME)
	    (void)add_message("%s\n", P_FNAME);
	else {
	    if(fptr == NULL) return(ERR);
	    (void)strcpy(P_FNAME, fptr);
	}
	break;
	
    case 'O':
    case 'i':
	if(*inptr != NL || P_NLINES > 1)
	    return(ERR);
	
	P_CUR_AUTOIND = P_AUTOINDFLG;
	if ( P_AUTOINDFLG ) count_blanks(P_LINE1);
	if(append(prevln(P_LINE1), glob) < 0)
	    return(ERR);
	P_FCHANGED = TRUE;
	break;
	
    case 'j':
	if (*inptr == 'p')
	{
	    inptr++;
	    pflag++;
	}
	if (*inptr != NL || deflt(P_CURLN, P_CURLN+1)<0)
	    return(ERR);
	if (join(P_LINE1, P_LINE2) < 0)
	    return(ERR);
	if (pflag || P_PFLG)
	    if (doprnt(P_CURLN, P_CURLN) < 0)
		return(ERR);
	break;
	
    case 'k':
	Skip_White_Space;
	
	if (*inptr < 'a' || *inptr > 'z')
	    return ERR;
	c= *inptr++;
	
	if(*inptr != ' ' && *inptr != HT && *inptr != NL)
	    return(ERR);
	
	P_MARK[c-'a'] = P_LINE1;
	break;
	
    case 'l':
	if(*inptr != NL)
	    return(ERR);
	if(deflt(P_CURLN,P_CURLN) < 0)
	    return(ERR);
	if (dolst(P_LINE1,P_LINE2) < 0)
	    return(ERR);
	break;
	
    case 'm':
	if((line3 = getone()) < 0)
	    return(ERR);
	if(deflt(P_CURLN,P_CURLN) < 0)
	    return(ERR);
	if(moveblock(line3) < 0)
	    return(ERR);
	P_FCHANGED = TRUE;
	break;
    case 'n':
	if(*inptr != NL)
	    return(ERR);
	P_FLAGS ^= NFLG_MASK;
	P_DIAG=!P_DIAG;
	(void)add_message("number %s, list %s\n",
 			  P_NFLG?"on":"off", P_LFLG?"on":"off");
	break;
	
#ifdef ED_INDENT
    case 'I':
	if(P_NLINES > 0)
	    return(ERR);
	if(*inptr != NL)
	    return(ERR);
	(void)add_message("Indenting entire code...\n");
	if (indent_code())
	    (void)add_message("Indention halted.\n");
	else 
	    (void)add_message("Done indenting.\n");
	break;
#endif
	
    case 'H':
    case 'h': 
	print_help(*(inptr++));
	break;
	
    case 'P':
    case 'p':
	if(*inptr != NL)
	    return(ERR);
	if(deflt(P_CURLN,P_CURLN) < 0)
	    return(ERR);
	if(doprnt(P_LINE1,P_LINE2) < 0)
	    return(ERR);
	break;
	
    case 'q':
	if (P_FCHANGED)
	    return CHANGED;
	/* FALLTHROUGH */
    case 'Q':
	(void)clrbuf();
	if(*inptr == NL && P_NLINES == 0 && !glob)
	    return(EOF);
	else
	    return(ERR);
	
    case 'r':
	if(P_NLINES > 1)
	    return(ERR);
	
	if(P_NLINES == 0)		/* The original code tested */
	    P_LINE2 = P_LASTLN;	/*	if(P_NLINES = 0)    */
	/* which looks wrong.  RAM  */
	
	if(*inptr != ' ' && *inptr != HT && *inptr != NL)
	    return(ERR);
	
	if((fptr = getfn(0)) == NULL)
	    return(ERR);
	
	if((err = doread(P_LINE2, fptr)) < 0)
	    return(err);
	P_FCHANGED = TRUE;
	break;
	
    case 's':
	if(*inptr == 'e')
	    return(set());
	Skip_White_Space;
	if((subpat = optpat()) == NULL)
	    return(ERR);
	if((gflag = getrhs(rhs)) < 0)
	    return(ERR);
	if(*inptr == 'p')
	    pflag++;
	if(deflt(P_CURLN, P_CURLN) < 0)
	    return(ERR);
	if ((nchng = subst(subpat, rhs, gflag, pflag || P_PFLG)) < 0)
	    return(ERR);
	if(nchng)
	    P_FCHANGED = TRUE;
	break;
	
    case 't':
	if((line3 = getone()) < 0)
	    return(ERR);
	if(deflt(P_CURLN,P_CURLN) < 0)
	    return(ERR);
	if(transfer(line3) < 0)
	    return(ERR);
	P_FCHANGED = TRUE;
	break;
	
    case 'W':
    case 'w':
	apflg = (c=='W');
	
	if(*inptr != ' ' && *inptr != HT && *inptr != NL)
	    return(ERR);
	
	if((fptr = getfn(1)) == NULL)
	    return(ERR);
	
	if(deflt(1, P_LASTLN) < 0)
	    return(ERR);
	if(dowrite(P_LINE1, P_LINE2, fptr, apflg) < 0)
	    return(ERR);
	P_FCHANGED = FALSE;
	break;
	
    case 'x':
	if(*inptr == NL && P_NLINES == 0 && !glob) {
	    if((fptr = getfn(1)) == NULL)
		return(ERR);
	    if(dowrite(1, P_LASTLN, fptr, 0) >= 0)
	    {
		(void)clrbuf();
		return(EOF);
	    }
	}
	return(ERR);
	
    case 'z':
	if(deflt(P_CURLN,P_CURLN) < 0)
	    return(ERR);
	
	switch(*inptr) {
	case '-':
	    if(doprnt(P_LINE1-21,P_LINE1) < 0)
		return(ERR);
	    break;
	    
	case '.':
	    if(doprnt(P_LINE1-11,P_LINE1+10) < 0)
		return(ERR);
	    break;
	    
	case '+':
	case '\n':
	    if(doprnt(P_LINE1,P_LINE1+21) < 0)
		return(ERR);
	    break;
	}
	break;
	
    case 'Z':
	if(deflt(P_CURLN,P_CURLN) < 0)
	    return(ERR);
	
	switch(*inptr) {
	case '-':
	    if(doprnt(P_LINE1-41,P_LINE1) < 0)
		return(ERR);
	    break;
	    
	case '.':
	    if(doprnt(P_LINE1-21,P_LINE1+20) < 0)
		return(ERR);
	    break;
	    
	case '+':
	case '\n':
	    if(doprnt(P_LINE1,P_LINE1+41) < 0)
		return(ERR);
	    break;
	}
	break;	
    default:
	return(ERR);
    }
    
    return (0);
}  /* docmd */


/*	doglob.c	*/
int
doglob()
{
    int	lin, status;
    char	*cmd;
    LINE	*ptr;
    
    cmd = inptr;
    
    for (;;)
    {
	ptr = getptr(1);
	for (lin=1; lin<=P_LASTLN; lin++)
	{
	    if (ptr->l_stat & LGLOB)
		break;
	    ptr = getnextptr(ptr);
	}
	if (lin > P_LASTLN)
	    break;
	
	ptr->l_stat &= ~LGLOB;
	P_CURLN = lin; P_CURPTR = ptr;
	inptr = cmd;
	if((status = getlst()) < 0)
	    return(status);
	if((status = docmd(1)) < 0)
	    return(status);
	}
	return(P_CURLN);
}  /* doglob */


/*
 * Start the editor. Because several players can edit simultaneously,
 * they will each need a separate editor data block.
 *
 * If an exit_func is given, then call it at
 * exit of editor. The purpose is to make it possible for external LPC
 * code to maintain a list of locked files.
 */
void 
ed_start(char *file_arg, struct closure *exit_func)
{
    struct svalue *setup;
    if (!command_giver->interactive)
	error("Tried to start an ed session on a non-interative player.\n");
    if (ED_BUFFER)
	error("Tried to start an ed session, when already active.\n");
    
    ED_BUFFER = (struct ed_buffer *)xalloc(sizeof (struct ed_buffer));
    (void)memset((char *)command_giver->interactive->ed_buffer, '\0',
		 sizeof (struct ed_buffer));
    ED_BUFFER->truncflg = 1;
    ED_BUFFER->flags |= EIGHTBIT_MASK | TABINDENT_MASK;
    ED_BUFFER->shiftwidth = 4;
    /*	push_object(command_giver, "ed: ed_start()"); */
    push_object(command_giver);
    setup = apply_master_ob(M_RETRIEVE_ED_SETUP,1);
    if ( setup && setup->type == T_NUMBER && setup->u.number )
    {
	ED_BUFFER->flags      = setup->u.number & ALL_FLAGS_MASK;
	ED_BUFFER->shiftwidth = setup->u.number & SHIFTWIDTH_MASK;
    }
    ED_BUFFER->CurPtr =	&ED_BUFFER->Line0;
    ED_BUFFER->exit_func = exit_func; /* reference count has been incremented already */
    set_ed_buf();
    
    /*
     * Check for read on startup, since the buffer is read in. But do not
     * check for write, since we may want to change the file name.
     * When in compatibility mode, we assume that the test of valid read
     * is done by the caller of ed().
     */
    if (file_arg &&
	(file_arg = check_valid_path(file_arg, command_giver,
				     "ed_start", 0)) != NULL)
    {
	if (!doread(0, file_arg))
	    setCurLn( 1 );
	else
	{	/* May be a new file. Check with master for starting text. */
	    push_object(command_giver);
	    push_string(file_arg, STRING_MSTRING);
	    if ((setup = apply_master_ob(M_GET_ED_EMPTY_FILE,2)) != NULL)
	    {
		int i;

		switch (setup->type)
		{
		    case T_POINTER:
			for (i = 0; i < setup->u.vec->size; ++i)
			    if (setup->u.vec->item[i].type == T_STRING)
				(void)ins(setup->u.vec->item[i].u.string);
			break;
		    case T_STRING:
			(void)ins(setup->u.string);
		}
		free_svalue(setup);
	    }
	}
    }
    if (file_arg)
    {
	(void)strncpy(P_FNAME, file_arg, MAXFNAME-1);
	P_FNAME[MAXFNAME-1] = 0;
    }
    else 
	(void)add_message("No file.\n");
    set_prompt(PROMPT);
    return;
}

static void
free_ed_buffer()
{
    struct gdexception exception_frame;
    struct closure *function;
    extern int eval_cost;

    (void)clrbuf();

    function = ED_BUFFER->exit_func;
    free((char *)ED_BUFFER);
    ED_BUFFER = 0;
    set_prompt("> ");

    if (function)
    {
	exception_frame.e_exception = NULL;
	exception_frame.e_catch = 1;

	if (setjmp(exception_frame.e_context))
	    clear_state();
	else
	{
	    exception = &exception_frame;
	    eval_cost = 0;

	    (void)call_var(0, function);
	}
	free_closure(function);
    }
    else
	(void)add_message("Exit from ed.\n");
    return;
}

void 
ed_cmd(char *str)
{
    char cmd[MAXLINE];
    int status;

    strncpy(cmd, str, MAXLINE - 1);
    
    if (P_MORE)
    {
	print_help2();
	return;
    }
    
    if (P_APPENDING)
    {
	more_append(cmd);
	return;
    }

    /* Is this needed? */
    if (strlen(cmd) < (MAXLINE - 2))
    {
        strcat(cmd, "\n");
    }
    
    strncpy(inlin, cmd, MAXLINE - 1);    
    inlin[MAXLINE - 1] = 0;
    inptr = inlin;
    if(getlst() >= 0) {
	if((status = ckglob()) != 0)
	{
	    if(status >= 0 && (status = doglob()) >= 0)
	    {
		setCurLn( status );
		return;
	    }
	}
	else
	{
	    if((status = docmd(0)) >= 0)
	    {
		if(status == 1)
		    (void)doprnt(P_CURLN, P_CURLN);
		return;
	    }
	}
    } else {
	status = UNSPEC_FAIL;
    }
    switch (status)
    {
    case EOF:
	free_ed_buffer();
	return;
    case FATAL:
	if (ED_BUFFER->exit_func)
	    free_closure(ED_BUFFER->exit_func);
	free((char *)ED_BUFFER);
	ED_BUFFER = 0;
	(void)add_message("FATAL ERROR\n");
	set_prompt(PROMPT);
	return;
    case CHANGED:
	(void)add_message("File has been changed.\n");
	break;
    case SET_FAIL:
	(void)add_message("`set' command failed.\n");
	break;
    case SUB_FAIL:
	(void)add_message("string substitution failed.\n");
	break;
    case MEM_FAIL:
	(void)add_message("Out of memory: text may have been lost.\n" );
	break;
    default:
	(void)add_message("Unrecognized or failed command.\n");
	/*  Unrecognized or failed command (this  */
	/*  is SOOOO much better than "?" 	  */
    }
}

void 
save_ed_buffer()
{
    struct svalue *stmp;
    char *fname;

    push_string(P_FNAME, STRING_SSTRING);
    stmp = apply_master_ob(M_GET_ED_BUFFER_SAVE_FILE_NAME,1);
    if (stmp) {
	if (stmp->type == T_STRING) {
	    fname = stmp->u.string;
	    if (*fname == '/') fname++;
	    (void)dowrite(1, P_LASTLN, fname , 0);
	}
/*	free_svalue(stmp, "save_ed_buffer"); */
	free_svalue(stmp);
    }
    free_ed_buffer();
}

static void
print_help(int arg)
{
    switch (arg)
    {
#ifdef ED_INDENT
    case 'I':
	(void)add_message("       Automatic Indentation (V 1.0)\n");
	(void)add_message("------------------------------------\n");
	(void)add_message("           by Qixx [Update: 7/10/91]\n");
	(void)add_message("\nBy using the command 'I', a program is run which will\n");
	(void)add_message("automatically indent all lines in your code.  As this is\n");
	(void)add_message("being done, the program will also search for some basic\n");
	(void)add_message("errors (which don't show up good during compiling) such as\n");
	(void)add_message("Unterminated String, Mismatched Brackets and Parentheses,\n");
	(void)add_message("and indented code is easy to understand and debug, since if\n");
	(void)add_message("your brackets are off -- the code will LOOK wrong. Please\n");
	(void)add_message("mail me at gaunt@mcs.anl.gov with any pieces of code which\n");
	(void)add_message("don't get indented properly.\n");
	break;
#endif
#if 0
    case '^':
	(void)add_message("Command: ^   Usage: ^pattern\n");
	(void)add_message("This command is similiar to grep, in that it searches the\n");
	(void)add_message("entire file, printing every line that contains the specified\n");
	(void)add_message("pattern.  To get the line numbers of found lines, turn on line\n");
	(void)add_message("number printing with the 'n' command.\n");
	break;
#endif
    case 'n':
	(void)add_message("Command: n   Usage: n\n");
	(void)add_message("This command toggles the internal flag which will cause line\n");
	(void)add_message("numbers to be printed whenever a line is listed.\n");
	break;
    case 'a':
	(void)add_message("Command: a   Usage: a\n");
	(void)add_message("Append causes the editor to enter input mode, inserting all text\n");
	(void)add_message("starting AFTER the current line. Use a '.' on a blank line to exit\n");
	(void)add_message("this mode.\n");
	break;
    case 'A':
	(void)add_message("Command: A   Usage: A\n\
Like the 'a' command, but uses inverse autoindent mode.\n");
	break;
    case 'i':
	(void)add_message("Command: i   Usage: i\n");
	(void)add_message("Insert causes the editor to enter input mode, inserting all text\n");
	(void)add_message("starting BEFORE the current line. Use a '.' on a blank line to exit\n");
	(void)add_message("this mode.\n");
	break;
    case 'c':
	(void)add_message("Command: c   Usage: c\n");
	(void)add_message("Change command causes the current line to be wiped from memory.\n");
	(void)add_message("The editor enters input mode and all text is inserted where the previous\n");
	(void)add_message("line existed.\n");
	break;
    case 'd':
	(void)add_message("Command: d   Usage: d  or [range]d\n");
	(void)add_message("Deletes the current line unless preceeded with a range of lines,\n");
	(void)add_message("then the entire range will be deleted.\n");
	break;
    case 'e':
	(void)add_message("Commmand: e  Usage: e filename\n");
	(void)add_message("Causes the current file to be wiped from memory, and the new file\n");
	(void)add_message("to be loaded in.\n");
	break;      
    case 'E':
	(void)add_message("Commmand: E  Usage: E filename\n");
	(void)add_message("Causes the current file to be wiped from memory, and the new file\n");
	(void)add_message("to be loaded in.  Different from 'e' in the fact that it will wipe\n");
	(void)add_message("the current file even if there are unsaved modifications.\n");
	break;
    case 'f':
	(void)add_message("Command: f  Usage: f  or f filename\n");
	(void)add_message("Display or set the current filename.   If  filename is given as \nan argument, the file (f) command changes the current filename to\nfilename; otherwise, it prints  the current filename.\n");
	break;
    case 'g':
	(void)add_message("Command: g  Usage: g/re/p\n");
	(void)add_message("Search in all lines for expression 're', and print\n");
	(void)add_message("every match. Command 'l' can also be given\n");
	(void)add_message("Unlike in unix ed, you can also supply a range of lines\n");
	(void)add_message("to search in\n");
	(void)add_message("Compare with command 'v'.\n");
	break;
    case 'h':
	(void)add_message("Command: h    Usage:  h  or hc (where c is a command)\n");
	(void)add_message("Help files added by Qixx.\n");
	break;
    case 'j':
	(void)add_message("Command: j    Usage: j or [range]j\n");
	(void)add_message("Join Lines. Remove the NEWLINE character  from  between the  two\naddressed lines.  The defaults are the current line and the line\nfollowing.  If exactly one address is given,  this  command does\nnothing.  The joined line is the resulting current line.\n");
	break;
    case 'k':
	(void)add_message("Command: k   Usage: kc  (where c is a character)\n");
	(void)add_message("Mark the addressed line with the name c,  a  lower-case\nletter.   The  address-form,  'c,  addresses  the  line\nmarked by c.  k accepts one address; the default is the\ncurrent line.  The current line is left unchanged.\n");
	break;
    case 'l':
	(void)add_message("Command: l   Usage: l  or  [range]l\n");
	(void)add_message("List the current line or a range of lines in an unambiguous\nway such that non-printing characters are represented as\nsymbols (specifically New-Lines).\n");
	break;
    case 'm':
	(void)add_message("Command: m   Usage: mADDRESS or [range]mADDRESS\n");
	(void)add_message("Move the current line (or range of lines if specified) to a\nlocation just after the specified ADDRESS.  Address 0 is the\nbeginning of the file and the default destination is the\ncurrent line.\n");
	break;
    case 'p':
	(void)add_message("Command: p    Usage: p  or  [range]p\n");
	(void)add_message("Print the current line (or range of lines if specified) to the\nscreen. See the command 'n' if line numbering is desired.\n");
	break;
    case 'q':
	(void)add_message("Command: q    Usage: q\n");
	(void)add_message("Quit the editor. Note that you can't quit this way if there\nare any unsaved changes.  See 'w' for writing changes to file.\n");
	break;
    case 'Q':
	(void)add_message("Command: Q    Usage: Q\n");
	(void)add_message("Force Quit.  Quit the editor even if the buffer contains unsaved\nmodifications.\n");
	break;
    case 'r':
	(void)add_message("Command: r    Usage: r filename\n");
	(void)add_message("Reads the given filename into the current buffer starting\nat the current line.\n");
	break;
    case 't':
	(void)add_message("Command: t   Usage: tADDRESS or [range]tADDRESS\n");
	(void)add_message("Transpose a copy of the current line (or range of lines if specified)\nto a location just after the specified ADDRESS.  Address 0 is the\nbeginning of the file and the default destination\nis the current line.\n");
	break;
    case 'v':
	(void)add_message("Command: v   Usage: v/re/p\n");
	(void)add_message("Search in all lines without expression 're', and print\n");
	(void)add_message("every match. Other commands than 'p' can also be given\n");
	(void)add_message("Compare with command 'g'.\n");
	break;
    case 'z':
	(void)add_message("Command: z   Usage: z  or  z-  or z.\n");
	(void)add_message("Displays 20 lines starting at the current line.\nIf the command is 'z.' then 20 lines are displayed being\ncentered on the current line. The command 'z-' displays\nthe 20 lines before the current line.\n");
	break;
    case 'Z':
	(void)add_message("Command: Z   Usage: Z  or  Z-  or Z.\n");
	(void)add_message("Displays 40 lines starting at the current line.\nIf the command is 'Z.' then 40 lines are displayed being\ncentered on the current line. The command 'Z-' displays\nthe 40 lines before the current line.\n");
	break;
    case 'x':
	(void)add_message("Command: x   Usage: x\n");
	(void)add_message("Save file under the current name, and then exit from ed.\n");
	break;
    case 's':
	if ( *inptr=='e' && *(inptr+1)=='t' ) {
	    (void)add_message("\
Without arguments: show current settings.\n\
'set save' will preserve the current settings for subsequent invocations of ed.\n\
Options:\n\
\n\
number	   will print line numbers before printing or inserting a lines\n\
list	   will print control characters in p(rint) and z command like in l(ist)\n\
print	   will show current line after a single substitution\n\
eightbit\n\
autoindent will preserve current indentation while entering text.\n\
	   use ^D or ^K to get back one step back to the right.\n\
excompatible will exchange the meaning of \\( and ( as well as \\) and )\n\
\n\
An option can be cleared by prepending it with 'no' in the set command, e.g.\n\
'set nolist' to turn off the list option.\n\
\n\
set shiftwidth <digit> will store <digit> in the shiftwidth variable, which\n\
determines how much blanks are removed from the current indentation when\n\
typing ^D or ^K in the autoindent mode.\n");
		break;
	}
	/* is there anyone who wants to add an exact description for the 's' command? */
	/* FALLTHROUGH */
    case 'w':
    case 'W':
    case '/':
    case '?':
	(void)add_message("Sorry no help yet for this command. Try again later.\n");
	break;
    default:
	(void)add_message("       Help for Ed  (V 2.0)\n");
	(void)add_message("---------------------------------\n");
	(void)add_message("     by Qixx [Update: 7/10/91]\n");
	(void)add_message("\n\nCommands\n--------\n");
	(void)add_message("/\tsearch forward for pattern\n");
	(void)add_message("?\tsearch backward for a pattern\n");
	/* (void)add_message("^\tglobal search and print for pattern\n"); */
	(void)add_message("=\tshow current line number\n");
	(void)add_message("a\tappend text starting after this line\n");
	(void)add_message("A\tlike 'a' but with inverse autoindent mode\n"),
	(void)add_message("c\tchange current line, query for replacement text\n");
	(void)add_message("d\tdelete line(s)\n");
	(void)add_message("e\treplace this file with another file\n");
	(void)add_message("E\tsame as 'e' but works if file has been modified\n");
	(void)add_message("f\tshow/change current file name\n");
	(void)add_message("g\tSearch and execute command on any matching line.\n");
	(void)add_message("h\thelp file (display this message)\n");
	(void)add_message("i\tinsert text starting before this line\n");
#ifdef ED_INDENT
	(void)add_message("I\tindent the entire code (Qixx version 1.0)\n");
#endif
	(void)add_message("\n--Return to continue--");
	P_MORE=1;
	break;
    }
}

static void 
print_help2() 
{
    P_MORE = 0;
    (void)add_message("j\tjoin lines together\n");
    (void)add_message("k\tmark this line with a character - later referenced as 'a\n");
    (void)add_message("l\tline line(s) with control characters displayed\n");
    (void)add_message("m\tmove line(s) to specified line\n");
    (void)add_message("n\ttoggle line numbering\n");
    (void)add_message("p\tprint line(s) in range\n");
    (void)add_message("q\tquit editor\n");
    (void)add_message("Q\tquit editor even if file modified and not saved\n\
r\tread file into editor at end of file or behind the given line\n");
    (void)add_message("s\tsearch and replace\n");
    (void)add_message("set\tquery, change or save option settings\n");
    (void)add_message("t\tmove copy of line(s) to specified line\n");
    (void)add_message("v\tSearch and execute command on any non-matching line.\n");
    (void)add_message("x\tsave file and quit\n");
    (void)add_message("w\twrite to current file (or specified file)\n");
    (void)add_message("W\tlike the 'w' command but appends instead\n");
    (void)add_message("z\tdisplay 20 lines, possible args are . + -\n");
    (void)add_message("Z\tdisplay 40 lines, possible args are . + -\n");
    (void)add_message("\nFor further information type 'hc' where c is the command\nthat help is desired for.\n");
}

