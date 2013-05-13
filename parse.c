/*

  Pattern Parser package for LPmud

  Ver 3.1

  If you have questions or complaints about this code please refer them
  to jna@cd.chalmers.se

*/

#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "interpret.h"
#include "object.h"
#include "exec.h"
#include "simulate.h"
#include "mapping.h"

#include "inline_svalue.h"

extern int d_flag; /* for debugging purposes */
extern struct object *previous_ob;
struct object *vbfc_object;

#ifndef tolower			/* On some systems this is a function */
extern int tolower (int);
#endif

/*****************************************************

  This is the parser used by the efun parse_command

*/
/*

  General documentation:

  parse_command() is one of the most complex efun in LPmud to use. It takes
  some effort to learn and use, but when mastered, very powerful constructs
  can be implemented.

  Basically parse_command() is a hotted sscanf operating on word basis. It
  works similar to sscanf in that it takes a pattern and a variable set of
  destination arguments. It is together with sscanf the only efun to use
  pass by reference for other variables than arrays.

  To make the efun useful it must have a certain support from the mudlib,
  there is a set of functions that it needs to call to get relevant
  information before it can parse in a sensible manner.

  In earlier versions it used the normal id() lfun in the LPC objects to
  find out if a given object was identified by a certain string. This was
  highly inefficient as it could result in hundreds or maybe thousands of
  calls when very long commands were parsed. 
  
  The new version relies on the LPC objects to give it three lists of 'names'.

       1 - The normal singular names.
       2 - The plural forms of the names.
       3 - The acknowledged adjectives of the object.

  These are fetched by calls to the functions:

       1 - string *parse_command_id_list();
       2 - string *parse_command_plural_id_list();
       3 - string *parse_command_adjectiv_id_list();

  The only really needed list is the first. If the second does not exist
  than the efun will try to create one from the singular list. For 
  grammatical reasons it does not always succeed in a perfect way. This is
  especially true when the 'names' are not single words but phrases.

  The third is very nice to have because it makes constructs like
  'get all the little blue ones' possible.

  Apart from these functions that should exist in all objects, and which
  are therefore best put in /std/object.c there is also a set of functions
  needed in /secure/master.c These are not absolutely necessary but they
  give extra power to the efun.

  Basically these /secure/master.c lfuns are there to give default values
  for the lists of names fetched from each object.

  The names in these lists are applicable to any and all objects, the first
  three are identical to the lfun's in the objects:

       string *parse_command_id_list()
                - Would normally return: ({ "one", "thing" })

       string *parse_command_plural_id_list()
                - Would normally return: ({ "ones", "things", "them" })

       string *parse_command_adjectiv_id_list()
                - Would normally return ({ "iffish" })

  The last two are the default list of the prepositions and a single so called
  'all' word. 

       string *parse_command_prepos_list()
                 - Would normally return: ({ "in", "on", "under" })

       string parse_command_all_word()
                 - Would normally return: "all"

  IF you want to use a different language than English but still want the
  default pluralform maker to work, you need to replace parse.c with the
  following file:

#if 0
    * Language configured parse.c
    *
    #define PARSE_FOREIGN
   
    char *parse_to_plural(str)
        char *str;
    {

        * Your own plural converter for your language *
     
    }

      * The numberwords below should be replaced for the new language *

    static char *ord1[] = {"", "first", "second", "third", "fourth", "fifth",
			   "sixth", "seventh", "eighth", "ninth", "tenth",
			   "eleventh", "twelfth", "thirteenth", "fourteenth",
			   "fifteenth", "sixteenth", "seventeenth", 
			   "eighteenth","nineteenth"};

    static char *ord10[] = {"", "", "twenty","thirty","forty","fifty","sixty",
			    "seventy", "eighty","ninety"};
    
    static char *sord10[] = {"", "", "twentieth", "thirtieth", "fortieth",
			     "fiftieth", "sixtieth","seventieth", "eightieth",
			     "ninetieth"};

    static char *num1[] = {"", "one","two","three","four","five","six",
			   "seven","eight","nine","ten",
			   "eleven","twelve","thirteen","fourteen","fifteen",
			   "sixteen", "seventeen","eighteen","nineteen"};

    static char *num10[] = {"", "", "twenty","thirty","forty","fifty","sixty",
			   "seventy", "eighty","ninety"};

    #include "parse_english.c"      * This parse.c file *

#endif
  
  When all these things are defined parse_command() works best and most
  efficient. What follows is the docs for how to use it from LPC:


  Doc for LPC function

int parse_command(string, object/object*, string, destargs...)

			Returns 1 if pattern matches

	string		Given command

	object*		if arr 
	object			array holding the accessible objects
			if ob
				object from which to recurse and create
				the list of accessible objects, normally
				ob = environment(this_player())
	string		Parsepattern as list of words and formats:
			Example string = " 'get' / 'take' %i "
			Syntax:
				'word' 		obligatory text
				[word]		optional text
				/		Alternative marker
				%o		Single item, object
				%l		Living objects
				%s		Any text
				%w              Any word
				%p		One of a list (prepositions)
				%i		Any items
				%d              Number 0- or tx(0-99)

	destargs	This is the list of result variables as in sscanf
			One variable is needed for each %_
			The return types of different %_ is:
			%o	Returns an object
			%s	Returns a string of words
			%w      Returns a string of one word
			%p	Can on entry hold a list of word in array
				or an empty variable
				Returns:
				   if empty variable: a string
				   if array: array[0]=matched word
			%i	Returns a special array on the form:
				[0] = (int) +(wanted) -(order) 0(all)
				[1..n] (object) Objectpointers	
			%l	Returns a special array on the form:
				[0] = (int) +(wanted) -(order) 0(all)
				[1..n] (object) Objectpointers
				                These are only living objects.
			%d      Returns a number

  The only types of % that uses all the loaded information from the objects
  are %i and %l. These are in fact identical except that %l filters out
  all nonliving objects from the list of objects before trying to parse.

  The return values of %i and %l is also the most complex. They return an
  array consisting of first a number and then all possible objects matching.
  As the typical string matched by %i/%l looks like: 'three red roses',
  'all nasty bugs' or 'second blue sword' the number indicates which 
  of these numerical constructs was matched:

         if numeral >0 then three, four, five etc were matched
         if numeral <0 then second, twentyfirst etc were matched
         if numeral==0 then 'all' or a generic plural form such as 'apples'
                            were matched.

  NOTE!
       The efun makes no semantic implication on the given numeral. It does
       not matter if 'all apples' or 'second apple' is given. A %i will
       return ALL possible objects matching in the array. It is up to the
       caller to decide what 'second' means in a given context.

       Also when given an object and not an explicit array of objects the
       entire recursive inventory of the given object is searched. It is up
       to the caller to decide which of the objects are actually visible
       meaning that 'second' might not at all mean the second object in
       the returned array of objects.
			
Example:

 if (parse_command("spray car",environment(this_player()),
                      " 'spray' / 'paint' [paint] %i ",items))  
 {
      If the pattern matched then items holds a return array as described
        under 'destargs' %i above.
     
 }

 BUGS / Features
:

 Patterns of type: "%s %w %i"
   Might not work as one would expect. %w will always succeed so the arg
   corresponding to %s will always be empty.

 Patterns of the type: 'word' and [word]
   The 'word' can not contain spaces. It must be a single word. This is so
   because the pattern is exploded on " " (space) and a pattern element can
   therefore not contain spaces.
            This will be fixed in the future


*/ 

/* Some useful string macros
*/
#define EQ(x,y) (strcmp(x,y)==0)

/* Function in LPC which returns a list of ids 
*/
#define QGET_ID "parse_command_id_list"		
#define M_QGET_ID M_PARSE_COMMAND_ID_LIST
/* Function in LPC which returns a list of plural ids 
*/
#define QGET_PLURID "parse_command_plural_id_list"   
#define M_QGET_PLURID M_PARSE_COMMAND_PLURAL_ID_LIST
/* Function in LPC which returns a list of adjectiv ids 
*/
#define QGET_ADJID "parse_command_adjectiv_id_list" 
#define M_QGET_ADJID M_PARSE_COMMAND_ADJECTIV_ID_LIST
/* Function in LPC which returns a list of prepositions
*/
#define QGET_PREPOS "parse_command_prepos_list" 
#define M_QGET_PREPOS M_PARSE_COMMAND_PREPOS_LIST
/* Function in LPC which returns the 'all' word
*/
#define QGET_ALLWORD "parse_command_all_word"
#define M_QGET_ALLWORD M_PARSE_COMMAND_ALL_WORD
/* Global vectors for 'caching' of ids

   The main 'parse' routine stores these on call, making the entire
   parse_command() reentrant.
*/
static struct vector	*gId_list	= 0;
static struct vector	*gPluid_list	= 0;
static struct vector	*gAdjid_list	= 0;

static struct vector	*gId_list_d	= 0;  /* From master */
static struct vector	*gPluid_list_d	= 0;  /* From master */
static struct vector	*gAdjid_list_d	= 0;  /* From master */
static struct vector	*gPrepos_list	= 0;  /* From master */
static char 		*gAllword       = 0;  /* From master */

/*
 * Master has been (re)loaded; fetch params needed for parsing
 */
void
load_parse_information()
{
    struct svalue *pval;

    free_parse_information();

    /* Get the default ids of 'general references' from master object
    */
    pval = apply_master_ob(M_QGET_ID, 0);
    if (pval && pval->type == T_POINTER)
    {
	gId_list_d = pval->u.vec;
	INCREF(pval->u.vec->ref);	    /* Otherwise next sapply will free it */
    }
    else
	gId_list_d = 0;

    pval = apply_master_ob(M_QGET_PLURID,0);
    if (pval && pval->type == T_POINTER)	
    {
	gPluid_list_d = pval->u.vec;
	INCREF(pval->u.vec->ref);          /* Otherwise next sapply will free it */
    }
    else
	gPluid_list_d = 0;

    pval = apply_master_ob(M_QGET_ADJID, 0);
    if (pval && pval->type == T_POINTER)
    {
	gAdjid_list_d = pval->u.vec;
	INCREF(pval->u.vec->ref);          /* Otherwise next sapply will free it */
    }
    else
	gAdjid_list_d = 0;

    pval = apply_master_ob(M_QGET_PREPOS,0);
    if (pval && pval->type == T_POINTER)
    {
	gPrepos_list = pval->u.vec;
	INCREF(pval->u.vec->ref);          /* Otherwise next sapply will free it */
    }
    else
	gPrepos_list = 0;

    pval = apply_master_ob(M_QGET_ALLWORD,0);
    if (pval && pval->type == T_STRING)
	gAllword = string_copy(pval->u.string);
    else
	gAllword = 0;
}

/*
 * Function name: 	load_lpc_info
 * Description:		Loads relevant information from a given object.
 *			This is the ids, plural ids and adjectiv ids. This
 *			is the only calls to LPC objects other than the
 *                      master object that occur within the efun
 *                      parse_command().
 * Arguments:		ix: Index in the array
 *			ob: The object to call for information.
 */
void
load_lpc_info(int ix, struct object *ob)
{
    struct vector *tmp, *sing;
    struct svalue sval, *ret;
    int il, make_plural = 0;
    char *str;
    char *parse_to_plural(char *);
    
    if (!ob)
	return;
    
    if (gPluid_list && 
	gPluid_list->size > ix && 
	gPluid_list->item[ix].type == T_NUMBER &&
	gPluid_list->item[ix].u.number == 0)
    {
	ret = apply(QGET_PLURID, ob, 0, 1);
	if (ret && ret->type == T_POINTER) 
	    assign_svalue_no_free(&gPluid_list->item[ix], ret);
	else 
	{
	    make_plural = 1;
	    gPluid_list->item[ix].u.number = 1;
	}
    }
    
    if (gId_list && 
	gId_list->size > ix && 
	gId_list->item[ix].type == T_NUMBER &&
	gId_list->item[ix].u.number == 0)
    {
	ret = apply(QGET_ID, ob, 0, 1);
	if (ret && ret->type == T_POINTER)
	{
	    assign_svalue_no_free(&gId_list->item[ix], ret);
	    if (make_plural)
	    {
		tmp = allocate_array(ret->u.vec->size);
		sing = ret->u.vec;
		
		sval.type = T_STRING;
		sval.string_type = STRING_MSTRING;
		for (il = 0; il < tmp->size; il++)
		{
		    if (sing->item[il].type == T_STRING)
		    {
			str = parse_to_plural(sing->item[il].u.string);
			sval.u.string = str;
			assign_svalue_no_free(&tmp->item[il],&sval);
			free_mstring(sval.u.string);
		    }
		}
		sval.type = T_POINTER;
		sval.u.vec = tmp;
		assign_svalue_no_free(&gPluid_list->item[ix], &sval);
		free_svalue(&sval);
	    }
	}
	else
	{
	    gId_list->item[ix].u.number = 1;
	}
    }
    
    if (gAdjid_list && 
	gAdjid_list->size > ix && 	
	gAdjid_list->item[ix].type == T_NUMBER &&
	gAdjid_list->item[ix].u.number == 0)
    {
	ret = apply(QGET_ADJID, ob, 0, 1);
	if (ret && ret->type == T_POINTER)
	    assign_svalue_no_free(&gAdjid_list->item[ix], ret);
	else
	    gAdjid_list->item[ix].u.number = 1;
    }
}

void
free_parse_information()
{
    if (gId_list_d) 
	free_vector(gId_list_d); 
    gId_list_d = 0;
    if (gPluid_list_d) 
	free_vector(gPluid_list_d); 
    gPluid_list_d = 0;
    if (gAdjid_list_d) 
	free_vector(gAdjid_list_d);
    gAdjid_list_d = 0;
    if (gPrepos_list) 
	free_vector(gPrepos_list);
    gPrepos_list = 0;
    if (gAllword)
	free(gAllword);
    gAllword = 0;
}

/* Main function, called from interpret.c
*/

/*
 * Function name: 	parse
 * Description:		The main function for the efun: parse_command()
 *			It parses a given command using a given pattern and
 *			a set of objects (see args below). For details
 *			see LPC documentation of the efun.
 * Arguments:		cmd: The command to parse
 *			ob_or_array: A list of objects or one object from 
 *			             which to make a list of objects by
 *				     using the objects deep_inventory
 *			pattern: The given parse pattern somewhat like sscanf
 *			         but with different %-codes, see efun docs.
 *			stack_args: Pointer to destination arguments.
 *			num_arg: Number of destination arguments.
 * Returns:		True if command matched pattern.
 */
int
parse (char *cmd, struct svalue *ob_or_array, char *pattern,
       struct svalue *stack_args, int num_arg)
/*
    char 		*cmd;          	Command to parse
    struct svalue 	*ob_or_array;  	Object or array of objects
    char		*pattern;	Special parsing pattern
    struct svalue 	*stack_args;	Pointer to lvalue args on stack
    int 		num_arg;	Number of args on stack
*/
{
    struct vector	*obvec, *patvec, *wvec;
    struct vector	*old_id, *old_plid, *old_adjid;
    int			pix, cix, six, fail, fword, ocix, fpix;
    struct svalue	*pval;
    void		check_for_destr(struct svalue *);    /* In interpret.c */
    struct svalue	*sub_parse(struct vector *, struct vector *, int *, struct vector *, int *, int *, struct svalue *);
    struct svalue	*slice_words(struct vector *, int, int), tmp;
    void		stack_put(struct svalue *, struct svalue *, int, int);
    struct vector	*deep_inventory(struct object *, int);

    /*
     * Pattern and commands can not be empty
     */
    if (*cmd == '\0' || *pattern == '\0')
	return 0;

    wvec = explode_string(cmd," ");        /* Array of words in command */
    patvec = explode_string(pattern," ");  /* Array of pattern elements */

    /*
     * Explode can return '0'. 
     */
    if (!wvec)
	wvec = allocate_array(0);
    if (!patvec)
	patvec = allocate_array(0);

    INCREF(wvec->ref); 		/* Do not lose these arrays */
    INCREF(patvec->ref);

    if (ob_or_array->type == T_POINTER)
	obvec = ob_or_array->u.vec;
    else if (ob_or_array->type == T_OBJECT)
	obvec = deep_inventory(ob_or_array->u.ob, 1); /* 1 == ob + deepinv */
    else
    {
	obvec = 0;
	error("Bad second argument to parse_command()\n");
    }

    tmp.type = T_POINTER;
    tmp.u.vec = obvec;
    check_for_destr(&tmp);

    INCREF(obvec->ref);

    /* Copy and  make space for id arrays
    */
    old_id      = gId_list; 
    old_plid    = gPluid_list; 
    old_adjid   = gAdjid_list;

    gId_list    = allocate_array(obvec->size);
    gPluid_list  = allocate_array(obvec->size);
    gAdjid_list = allocate_array(obvec->size);
    
    /* Loop through the pattern. Handle %s but not '/'
    */
    pix = 0;
#ifndef OLD_EXPLODE
    while (pix < patvec->size && !*(patvec->item[pix].u.string))
	pix++;
#endif

    six = 0;
    cix = 0;
    fail = 0;
    for (; pix < patvec->size; pix++)
    {
	pval = 0; 		/* The 'fill-in' value */
	fail = 0; 		/* 1 if match failed */

	if (EQ(patvec->item[pix].u.string, "%s"))
	{
	    /* We are at end of pattern, scrap up the remaining
	       words and put them in the fill-in value.
            */
	    if (pix == (patvec->size-1))
	    {
		pval = slice_words(wvec, cix, wvec->size - 1);
		cix = wvec->size;
		stack_put(pval, stack_args, six++, num_arg);
	    }
	    else 
	    /* 
	       There is something after %s, try to parse with the
               next pattern. Begin with the current word and step
	       one word for each fail, until match or end of words.
            */
	    {
		ocix = fword = cix; 	/* Current word */
		fpix = ++pix;		/* pix == next pattern */
		do
		{
		    fail = 0;
		    /*
			Parse the following pattern, fill-in values:
		        stack_args[six] = result of %s
		        stack_args[six + 1] = result of following pattern,
		    			      if it is a fill-in pattern
		     */
		    pval = sub_parse(obvec, patvec, &pix, wvec, &cix, &fail,
				     ((six + 1) < num_arg) ? 
				     stack_args[six + 1].u.lvalue
				     : 0);
		    if (fail)
		    {
			cix = ++ocix;
			pix = fpix;
		    }
		} while ((fail) && (cix < wvec->size));

		/* 
		    We found something matching the pattern after %s.
		    First
		        stack_args[six + 1] = result of match
		    Then
		        stack_args[six] = the skipped words before match
		*/
		if (!fail)
		{
		    /* A match with a value fill in param */
		    if (pval)
		    {
			stack_put(pval, stack_args, six + 1, num_arg);
			pval = slice_words(wvec, fword, ocix - 1);
			stack_put(pval, stack_args, six, num_arg);
			six += 2;
		    }
		    else
		    {
			/* A match with a non value ie 'word' */
			pval = slice_words(wvec, fword, ocix - 1);
			stack_put(pval, stack_args, six++, num_arg);
		    }
		    pval = 0; 
		}
	    }
	}

	else if (!EQ(patvec->item[pix].u.string,"/"))
	{
	    /* The pattern was not %s, parse the pattern if
	     * it is not '/', a '/' here is skipped.
	     * If match, put in fill-in value.
             */
	    pval = sub_parse(obvec, patvec, &pix, wvec, &cix, &fail,
			     (six < num_arg) ? stack_args[six].u.lvalue : 0);
	    if (!fail && pval)
		stack_put(pval, stack_args, six++, num_arg);
	}

	/* Terminate parsing if no match
        */
	if (fail)
	    break;
    }
  
    /* Also fail when there are words left to parse and pattern exhausted
    */
    if (cix < wvec->size)
	fail = 1;

    /* Delete and free the id arrays
    */
    if (gId_list) 
    {
	free_vector(gId_list);
    }
    if (gPluid_list) 
    {
	free_vector(gPluid_list);
    }
    if (gAdjid_list) 
    {
	free_vector(gAdjid_list);
    }

    gId_list = old_id;
    gPluid_list = old_plid; 
    gAdjid_list = old_adjid;

    DECREF(wvec->ref); 
    DECREF(patvec->ref);
    DECREF(obvec->ref);
    free_vector(wvec);
    free_vector(patvec);

    /*
     * A vector we made should be freed
     */
    if (ob_or_array->type == T_OBJECT)
    {
	free_vector(obvec); 
    }

    return !fail;
}

/*
 * Function name: 	stack_put
 * Description:		Puts an svalue on the stack.
 * Arguments:		pval: Value to put
 *			sp: Stackpointer
 *			pos: Position on stack to put value
 *			max: The number of args on the stack
 */
void 
stack_put(struct svalue	*pval, struct svalue *msp, int pos, int max)
{
    if (pos >= max)
	return;

    if ((pval) && (msp[pos].type == T_LVALUE))
	assign_svalue(msp[pos].u.lvalue, pval);
}

/*
 * Function name: 	slice_words
 * Description:		Gives an imploded string of words from an array
 * Arguments:		wvec: array of words
 *			from: First word to use
 *			to:   Last word to use
 * Returns:		A pointer to a static svalue now containing string.
 */
struct svalue *
slice_words(struct vector *wvec, int from, int to)
{
    struct vector	*slice;
    char		*tx;
    static struct svalue stmp = { T_NUMBER };

    tx = 0;

    if (from <= to)
    {
	slice = slice_array(wvec, from, to);

	if (slice->size)
	    tx = implode_string(slice, " ");
	free_vector(slice);
    }

    free_svalue(&stmp); /* May be allocated! */

    if (tx)
    {
	stmp.type = T_STRING;
	stmp.string_type = STRING_MSTRING;
	stmp.u.string = tx;
    }
    else
    {
	stmp.type = T_STRING;
	stmp.string_type = STRING_CSTRING;
	stmp.u.string = "";
    }
    return &stmp;
}

/*
 * Function name: 	sub_parse
 * Description:		Parses a vector of words against a pattern. Gives
 *			result as an svalue. Sets fail if parsing fails and
 *			updates pointers in pattern and word vectors. It
 *			handles alternate patterns but not "%s"
 */
struct svalue *
sub_parse(struct vector *obvec, struct vector *patvec, int *pix_in,
	  struct vector *wvec, int *cix_in, int *fail, struct svalue *msp)
{
    int			cix, pix, subfail;
    struct svalue	*pval;
    struct svalue	*one_parse(struct vector *, char *, struct vector *,
				   int *, int *, struct svalue *);

    cix = *cix_in; pix = *pix_in; subfail = 0;

    pval = one_parse(obvec, patvec->item[pix].u.string,
		     wvec, &cix, &subfail, msp);

    while (subfail)
    {
	pix++;
	cix = *cix_in;

	/*
	    Find the next alternative pattern, consecutive '/' are skipped
	 */
	while ((pix < patvec->size) && (EQ(patvec->item[pix].u.string, "/")))
	{
	    subfail = 0;
	    pix++;
	}

	if (!subfail && (pix < patvec->size))
	{
	    pval = one_parse(obvec, patvec->item[pix].u.string, wvec, &cix, 
			     &subfail, msp);
	}
	else if (subfail == 2) /* failed optional */
	{
	    subfail = 0;
	    pix = pix-1;
	}
	else
	{
	    *fail = 1; *pix_in = pix-1;
	    return 0;
	}
    }

    /* If there are alternatives left after the matching pattern, skip them
    */
    if ((pix + 1 < patvec->size) && (EQ(patvec->item[pix + 1].u.string, "/")))
    {
	while ((pix + 1 <patvec->size) &&
	       (EQ(patvec->item[pix + 1].u.string, "/")))
	{
	       pix += 2;
	}
	if (pix>=patvec->size)
	    pix = patvec->size-1;
    }

    *cix_in = cix;
    *pix_in = pix;
    *fail = 0;
    return pval;
}
    

/*
 * Function name: 	one_parse
 * Description:		Checks one parse pattern to see if match. Consumes
 *			needed number of words from wvec.
 * Arguments:		obvec: Vector of objects relevant to parse
 *			pat: The pattern to match against.
 *			wvec: Vector of words in the command to parse
 *			cix_in: Current word in commandword vector
 *			fail: Fail flag if parse did not match
 *			prep_param: Only used on %p (see prepos_parse)
 * Returns:		svalue holding result of parse.
 */
struct svalue *
one_parse(struct vector *obvec, char *pat, struct vector *wvec, int *cix_in,
	  int *fail, struct svalue *prep_param)
{
    char ch;
    struct svalue	*pval;
    static struct svalue stmp = { T_NUMBER };
    char		*str1, *str2;
    struct svalue	*item_parse(struct vector *, struct vector *, int *, int *);
    struct svalue	*living_parse(struct vector *, struct vector *, int *, int *);
    struct svalue	*single_parse(struct vector *, struct vector *, int *, int *);
    struct svalue	*prepos_parse(struct vector *, int *, int *, struct svalue *);
    struct svalue	*number_parse(struct vector *, int *, int *);

    /*
	Fail if we have a pattern left but no words to parse
     */
    if (*cix_in >= wvec->size) 
    {
	if (pat[0] == '[')
	    *fail = 0;
	else
	    *fail = 1;
	return 0;
    }

    pval = 0;
    ch = (pat[0] == '%' ? pat[1] : pat[0]); 

    switch (ch)
    {
    case 'i':
	pval = item_parse(obvec, wvec, cix_in, fail);
	break;

    case 'l':
	pval = living_parse(obvec, wvec, cix_in, fail);
	break;

    case 's':
	*fail = 0; /* This is double %s in pattern, skip it */
	break;

    case 'w':
	free_svalue(&stmp);
	stmp.type = T_STRING;
	stmp.string_type = STRING_SSTRING;
	stmp.u.string = make_sstring(wvec->item[*cix_in].u.string);
	pval = &stmp;
	(*cix_in)++;
	*fail = 0;
	break;

    case 'o':
	pval = single_parse(obvec, wvec, cix_in, fail);
	break;

    case 'p':
	pval = prepos_parse(wvec, cix_in, fail, prep_param);
	break;

    case 'd':
	pval = number_parse(wvec, cix_in, fail);
	break;

    case '\'':
	str1 = &pat[1]; str2 = wvec->item[*cix_in].u.string;
	if ((strncmp(str1, str2, strlen(str1) - 1) == 0) &&
	    (strlen(str1) == strlen(str2) + 1))
	{
	    *fail = 0;
	    (*cix_in)++;
	}
	else
	    *fail = 1;
	break;

    case '[':
	str1 = &pat[1]; str2 = wvec->item[*cix_in].u.string;
	if ((strncmp(str1, str2, strlen(str1) - 1) == 0) &&
	    (strlen(str1) == strlen(str2) + 1))
	{
	    (*cix_in)++;
	    *fail = 0;
	}
	else
	{
	    *fail = 2;
	}
	break;

    default:
	*fail = 0; /* Skip invalid patterns */
    }
    return pval;
}

/*
   We normally define these, see initial documentation (top of file)
*/
#ifndef PARSE_FOREIGN

    static char *ord1[] = {"", "first", "second", "third", "fourth", "fifth",
			   "sixth", "seventh", "eighth", "ninth", "tenth",
			   "eleventh", "twelfth", "thirteenth", "fourteenth",
			   "fifteenth", "sixteenth", "seventeenth", 
			   "eighteenth","nineteenth"};

    static char *ord10[] = {"", "", "twenty","thirty","forty","fifty","sixty",
			    "seventy", "eighty","ninety"};
    
    static char *sord10[] = {"", "", "twentieth", "thirtieth", "fortieth",
			     "fiftieth", "sixtieth","seventieth", "eightieth",
			     "ninetieth"};

    static char *num1[] = {"", "one","two","three","four","five","six",
			   "seven","eight","nine","ten",
			   "eleven","twelve","thirteen","fourteen","fifteen",
			   "sixteen", "seventeen","eighteen","nineteen"};

    static char *num10[] = {"", "", "twenty","thirty","forty","fifty","sixty",
			   "seventy", "eighty","ninety"};
#endif

/*
 * Function name: 	number_parse
 * Description:		Tries to interpret the word in wvec as a numeral
 *			descriptor and returns the result on the form:
 *			ret.type == T_NUMBER
 *			    num == 0, 'zero', '0', gAllword
 *			    num > 0, one, two, three etc or numbers given
 *			    num < 0, first, second,third etc given
 * Arguments:		wvec: Vector of words in the command to parse
 *			cix_in: Current word in commandword vector
 *			fail: Fail flag if parse did not match
 * Returns:		svalue holding result of parse.
 */
struct svalue *
number_parse(struct vector *wvec, int *cix_in, int *fail)
{
    int cix, ten, ones;
    long long num;
    char buf[100];
    static struct svalue stmp;	/* No need to free, only numbers */

    cix = *cix_in; *fail = 0;

    if (sscanf(wvec->item[cix].u.string, "%lld", &num))
    {
	if (num > 0)
	{
	    (*cix_in)++;
	    stmp.type = T_NUMBER;
	    stmp.u.number = num;
	    return &stmp;
	}
	*fail = 1;
	return 0; /* Only positive numbers */
    }

    if (gAllword && (strcmp(wvec->item[cix].u.string, gAllword) == 0))
    {
	(*cix_in)++;
	stmp.type = T_NUMBER;
	stmp.u.number = 0;
	return &stmp;
    }
    
    for (ten = 0; ten < 10; ten++)
	for(ones = 0; ones < 10; ones++)
	{
	    (void)sprintf(buf,"%s%s", num10[ten],
		    (ten > 1) ? num1[ones] : num1[ten * 10 + ones]);
	    if (EQ(buf, wvec->item[cix].u.string))
	    {
		(*cix_in)++;
		stmp.type = T_NUMBER;
		stmp.u.number = ten * 10 + ones;
		return &stmp;
	    }
	}
    
    for (ten = 0; ten < 10; ten++)
	for(ones = 0; ones < 10; ones++)
	{
	    (void)sprintf(buf,"%s%s", (ones) ? ord10[ten] : sord10[ten],
		    (ten > 1) ? ord1[ones] : ord1[ten * 10 + ones]);
	    if (EQ(buf, wvec->item[cix].u.string))
	    {
		(*cix_in)++;
		stmp.type = T_NUMBER;
		stmp.u.number = -(ten * 10 + ones);
		return &stmp;
	    }
	}

    *fail = 1;
    return 0;
}


/*
 * Function name: 	item_parse
 * Description:		Tries to match as many objects in obvec as possible
 *			onto the description given in commandvector wvec.
 *			Also finds numeral description if one exist and returns
 *			that as first element in array:
 *			ret[0].type == T_NUMBER
 *			    num == 0, 'all' or 'general plural given'
 *			    num > 0, one, two, three etc given
 *			    num < 0, first, second,third etc given
 *			ret[1-n] == Selected objectpointers from obvec
 * Arguments:		obvec: Vector of objects relevant to parse
 *			wvec: Vector of words in the command to parse
 *			cix_in: Current word in commandword vector
 *			fail: Fail flag if parse did not match
 * Returns:		svalue holding result of parse.
 */
struct svalue *
item_parse(struct vector *obvec, struct vector *wvec, int *cix_in, int *fail)
{
    struct vector	*tmp, *ret;
    struct svalue	*pval;
    static struct svalue stmp = { T_NUMBER };
    int			cix, tix, obix, plur_flag, max_cix, match_all;
    int			match_object(int, struct vector *, int *, int *);

    tmp = allocate_array(obvec->size + 1);
    if ( (pval = number_parse(wvec, cix_in, fail)) != NULL )
	assign_svalue_no_free(&tmp->item[0],pval);

    if ((pval) && (pval->u.number>1))
    {
	plur_flag = 1;
	match_all = 0;
    }
    else if ((pval) && (pval->u.number == 0))
    {
	plur_flag = 1;
	match_all = 1;
    }
    else 
    {
	plur_flag = 0;
	match_all = 0;
    }

    max_cix = *cix_in;
    tix = 1;
    for (obix = 0; obix < obvec->size; obix++)
    {
	*fail = 0; cix = *cix_in;
	if (obvec->item[obix].type != T_OBJECT)
	    continue;
	if (cix == wvec->size && match_all)
	{
	    assign_svalue_no_free(&tmp->item[tix++], &obvec->item[obix]);
	    continue;
	}	    
	load_lpc_info(obix, obvec->item[obix].u.ob);

	if (match_object(obix, wvec, &cix, &plur_flag))
	{
	    assign_svalue_no_free(&tmp->item[tix++], &obvec->item[obix]);
	    max_cix = (max_cix<cix) ? cix : max_cix;
	}
    }

    if (tix < 2)
    {
	*fail = 1;
	free_vector(tmp);
	if (pval)
	    (*cix_in)--;
	return 0;
    }
    else
    {
	if (*cix_in < wvec->size)
	    *cix_in = max_cix + 1;
	ret = slice_array(tmp, 0, tix - 1);
	if (!pval)
	{
	    ret->item[0].type = T_NUMBER;
	    ret->item[0].u.number = plur_flag ? 0 : 1;
	}
	free_vector(tmp);
    }

    /* stmp is static, and may contain old info that must be freed 
    */
    free_svalue(&stmp);
    stmp.type = T_POINTER;
    stmp.u.vec = ret;
    return &stmp;
}

/*
 * Function name: 	living_parse
 * Description:		Tries to match as many living objects in obvec as
 *			possible onto the description given in the command-
 *			vector wvec.
 *			Also finds numeral description if one exist and returns
 *			that as first element in array:
 *			ret[0].type == T_NUMBER
 *			    num == 0, 'all' or 'general plural given'
 *			    num > 0, one, two, three etc given
 *			    num < 0, first, second,third etc given
 *			ret[1-n] == Selected objectpointers from obvec
 *			If not found in obvec a find_player and
 *			lastly a find_living is done. These will return an
 *			objecttype svalue.
 * Arguments:		obvec: Vector of objects relevant to parse
 *			wvec: Vector of words in the command to parse
 *			cix_in: Current word in commandword vector
 *			fail: Fail flag if parse did not match
 * Returns:		svalue holding result of parse.
 */
struct svalue *
living_parse(struct vector *obvec, struct vector *wvec, int *cix_in, int *fail)
{
    struct vector	*live;
    struct svalue	*pval;
    static struct svalue stmp = { T_NUMBER };
    struct object	*ob;
    int			obix, tix;

    live = allocate_array(obvec->size);
    tix = 0; 
    *fail = 0;

    for (obix = 0; obix < obvec->size; obix++) 
	if (obvec->item[obix].type == T_OBJECT &&
	    obvec->item[obix].u.ob->flags & O_ENABLE_COMMANDS)
	    assign_svalue_no_free(&live->item[tix++], &obvec->item[obix]);

    if (tix)
    {
	pval = item_parse(live, wvec, cix_in, fail);
	if (pval)
	{
	    free_vector(live);
	    return pval;
	}
    }
    
    free_vector(live);

    /* find_living */
    ob = find_living_object(wvec->item[*cix_in].u.string);
    if (ob)
    {
	free_svalue(&stmp); 		/* Might be allocated */
	stmp.type = T_OBJECT;
	stmp.u.ob = ob;
	add_ref(ob, "living_parse");
	(*cix_in)++;
	return &stmp;
    }
    *fail = 1;
    return 0;
}

/*
 * Function name: 	single_parse
 * Description:		Finds the first object in obvec fitting the description
 *			in commandvector wvec. Gives this as an objectpointer.
 * Arguments:		obvec: Vector of objects relevant to parse
 *			wvec: Vector of words in the command to parse
 *			cix_in: Current word in commandword vector
 *			fail: Fail flag if parse did not match
 * Returns:		svalue holding result of parse.
 */
struct svalue *
single_parse(struct vector *obvec, struct vector *wvec, int *cix_in, int *fail)
{
    int			cix, obix, plur_flag;
    int			match_object(int, struct vector *, int *, int *);

    for (obix = 0; obix < obvec->size; obix++)
    {
	if (obvec->item[obix].type != T_OBJECT)
	    continue;
	*fail = 0; 
	cix = *cix_in;
	load_lpc_info(obix, obvec->item[obix].u.ob);
	plur_flag = 0;
	if (match_object(obix, wvec, &cix, &plur_flag))
	{
	    *cix_in = cix + 1;
	    return &obvec->item[obix];
	}
    }
    *fail = 1;
    return 0;
}


/*
 * Function name: 	prepos_parse
 * Description:		This is a general sentencelist matcher with some hard-
 *			coded prepositions as the default list. The list is 
 *			sent as a parameter which will be replaced in the
 *			destination values. If no list is given the return
 *			value on match with the hardcoded prepositions will be
 *			string. If a list is given, the list will be returned
 *			with the matched sentence swapped to the first element.
 * Arguments:		wvec: Vector of words in the command to parse
 *			cix_in: Current word in commandword vector
 *			fail: Fail flag if parse did not match
 *			prepos: Pointer to svalue holding prepos parameter.
 * Returns:		svalue holding result of parse.
 */
struct svalue *
prepos_parse(struct vector *wvec, int *cix_in, int *fail,
	     struct svalue *prepos)
{
    struct vector	*pvec, *tvec;
    static struct svalue stmp = { T_NUMBER };
    char *tmp;
    int pix, tix;
    
    if ((!prepos) || (prepos->type != T_POINTER))
    {
	pvec = gPrepos_list;
    }
    else
    {
	pvec = prepos->u.vec;
    }
    
    for (pix = 0; pix < pvec->size; pix++)
    {
	if (pvec->item[pix].type != T_STRING)
	    continue;
	
	tmp = pvec->item[pix].u.string;
	if (strchr(tmp,' ') == NULL)
	{
	    if (EQ(tmp, wvec->item[*cix_in].u.string))
	    {
		(*cix_in)++;
		break;
	    }
	}
	else 
	{
	    tvec = explode_string(tmp, " ");
	    for (tix = 0; tix < tvec->size; tix++)
	    {
		if ((*cix_in+tix >= wvec->size) ||
		    (!EQ(wvec->item[*cix_in+tix].u.string, tvec->item[tix].u.string)))
		    break;
	    }
	    if ( (tix = (tix == tvec->size) ? 1 : 0) != 0 )
		(*cix_in) += tvec->size;
	    free_vector(tvec);
	    if (tix)
		break;
	}
    }
    
    free_svalue(&stmp);
    
    if (pix == pvec->size)
    {
	stmp.type = T_NUMBER;
	stmp.u.number = 0;
	*fail = 1;
    }
    else if (pvec != gPrepos_list)
    {
	stmp = pvec->item[0];
	pvec->item[0] = pvec->item[pix];
	pvec->item[pix] = stmp;
	*fail = 0;
	assign_svalue_no_free(&stmp, prepos);
    }
    else
    {
	stmp.type = T_STRING;
	stmp.string_type = STRING_MSTRING;
	stmp.u.string = make_mstring(pvec->item[pix].u.string);
	*fail = 0;
    }
    
    return &stmp;
    
}

/*
 * Function name: 	match_object
 * Description:		Tests if a given object matches the description as
 *			given in the commandvector wvec.
 * Arguments:		obix: Index in id arrays for this object.
 *			wvec: Vector of words in the command to parse
 *			cix_in: Current word in commandword vector
 *			plur: This arg gets set if the noun was on pluralform
 * Returns:		True if object matches.
 */
int 
match_object(int obix, struct vector *wvec, int *cix_in, int *plur)
{
    struct vector	*ids;
    int 		il, pos, cplur, old_cix;
    char		*str;
    int			find_string(char *, struct vector *, int *);
    int			check_adjectiv(int, struct vector *, int, int);

    for (cplur = (*plur * 2); cplur < 4; cplur++)
    {
	switch (cplur) 
	{
	case 0:
	    if (!gId_list_d)
		continue;
	    ids = gId_list_d;
	    break;

	case 1:
	    if (!gId_list || 
		gId_list->size <= obix || 
		gId_list->item[obix].type != T_POINTER)
		continue;
	    ids = gId_list->item[obix].u.vec;
	    break;

	case 2:
	    if (!gPluid_list_d)
		continue;
	    ids = gPluid_list_d;
	    break;

	case 3:
	    if (!gPluid_list || 
		gPluid_list->size <= obix || 
		gPluid_list->item[obix].type != T_POINTER)
		continue;
	    ids = gPluid_list->item[obix].u.vec;
	    break;

	default:
	    ids = 0;

	}

	for (il = 0; il < ids->size; il++)
	{
	    if (ids->item[il].type == T_STRING)
	    {
		str = ids->item[il].u.string;  /* A given id of the object */
		old_cix = *cix_in;
		if ((pos = find_string(str, wvec, cix_in)) >= 0)
		{
		    if (pos == old_cix)
		    {
			if (cplur > 1)
			    *plur = 1;
			return 1;
		    }
		    else if (check_adjectiv(obix, wvec, old_cix, pos-1))
		    {
			if (cplur > 1)
			    *plur = 1;
			return 1;
		    }
		}
		*cix_in = old_cix;
	    }
	}
    }
    return 0;
}
			
		
/*
 * Function name: 	find_string
 * Description:		Finds out if a given string exist within an
 *			array of words.
 * Arguments:		str: String of some words 
 *			wvec: Array of words
 *			cix_in: Startpos in word array
 * Returns:		Pos in array if string found or -1
 */
int 
find_string(char *str, struct vector *wvec, int *cix_in)
{
    int fpos;
    char *p1;
    struct vector *split;

#ifndef OLD_EXPLODE
    while (*cix_in < wvec->size && !*(wvec->item[*cix_in].u.string))
	(*cix_in)++;
#endif

    for (; *cix_in < wvec->size; (*cix_in)++)
    {
	p1 = wvec->item[*cix_in].u.string;
	if (p1[0] != str[0])
	    continue;

	if (strcmp(p1, str) == 0) /* str was one word and we found it */
	    return *cix_in;

	if (strchr(str,' ') == NULL)
	    continue;

	/* If str was multi word we need to make som special checks
        */
	if (*cix_in == (wvec->size -1))
	    continue;

	split = explode_string(str," ");

	/*
	    wvec->size - *cix_in ==	2: One extra word
				3: Two extra words
        */
	if (!split || (split->size > (wvec->size - *cix_in))) 
	{
	    if (split)
		free_vector(split);
	    continue;
	}
	
	fpos = *cix_in;
	for (; (*cix_in-fpos) < split->size; (*cix_in)++)
	{
	    if (strcmp(split->item[*cix_in-fpos].u.string, 
		       wvec->item[*cix_in].u.string))
		break;
	}
	if ((*cix_in - fpos) == split->size) {
	    free_vector(split);
	    return fpos;
	}
	free_vector(split);

	*cix_in = fpos;

    }
    return -1;
}

/*
 * Function name: 	check_adjectiv
 * Description:		Checks a word to see if it fits as adjectiv of an
 *			object.
 * Arguments:		obix: The index in the global id arrays
 *			wvec: The command words
 *			from: #1 cmdword to test
 *			to:   last cmdword to test
 * Returns:		True if a match is made.
 */
int 
check_adjectiv(int obix, struct vector *wvec, int from, int to)
{
    int il, back, fail;
    char *adstr;
    size_t sum;
    struct vector *ids;
    int member_string(char *, struct vector *);

    if (gAdjid_list->item[obix].type == T_POINTER)
	ids = gAdjid_list->item[obix].u.vec;
    else
	ids = 0;

    sum = 0;
    fail = 0;
    for (il = from; il<= to; il++) 
    {
	sum += strlen(wvec->item[il].u.string) + 1;
	if ((member_string(wvec->item[il].u.string, ids) < 0) &&
	    (member_string(wvec->item[il].u.string, gAdjid_list_d) < 0))
	{
	    fail = 1;
	}
    }

    /* Simple case: all adjs were single word
    */
    if (!fail)
	return 1;    

    if (from == to)
	return 0;

    adstr = alloca(sum); 

    /*
     * If we now have: "adj1 adj2 adj3 ... adjN"
     * We must test in order:
     *               "adj1 adj2 adj3 .... adjN-1 adjN"
     *               "adj1 adj2 adj3 .... adjN-1"
     *               "adj1 adj2 adj3 ...."
     *                     ....             if match for adj1 .. adj3
     *                                      continue with:
     *
     *               "adj4 adj5 .... adjN-1 adjN"
     *               "adj4 adj5 .... adjN-1"
     *               "adj4 adj5 ...."
     *                      .....
     *
     */
#if 0
    for (il = from; il <= to;)                /* adj1 .. adjN */
#endif
    {
	for (back = to; back >= il; back--)   /* back from adjN to adj[il] */ 
	{
	    /* 
             * Create teststring with "adj[il] .. adj[back]"
             */
	    (void)strcpy(adstr, "");
	    for (sum = il; sum <= back; sum++) /* test "adj[il] .. adj[back] */
	    {
		if (sum > il)
		    (void)strcat(adstr, " ");
		(void)strcat(adstr, wvec->item[sum].u.string);
	    }
	    if ((member_string(adstr, ids) < 0) &&
		(member_string(adstr, gAdjid_list_d) < 0))
		continue;
            else
	    {
		il = back + 1;  /* Match "adj[il] adj[il+1] .. adj[back]" */
                back = 0;
		break;
	    }
	}
        if (back)
	   return 0;		/* adj[il] does not match at all => no match */
    }
    return 1;
}


/*
 * Function name: 	member_string
 * Description:		Checks if a string is a member of an array.
 * Arguments:		str: The string to search for
 *			svec: vector of strings
 * Returns:		Pos if found else -1.
 */
int 
member_string(char *str, struct vector *svec)
{
    int il;

    if (!svec)
	return -1;

    for (il = 0; il < svec->size; il++)
    {
	if (svec->item[il].type != T_STRING)
	    continue;

	if (strcmp(svec->item[il].u.string, str) == 0)
	    return il;
    }
    return -1;
}

#ifndef PARSE_FOREIGN
/*
 * Function name: 	parse_to_plural
 * Description:		Change a sentence in singular form to a sentence
 *			in pluralform.
 * Arguments:		str: The sentence to change
 * Returns:		Sentence in plural form.
 */
char *
parse_to_plural(char *str)
{
    struct vector	*words;
    struct svalue	stmp;
    char *spl;
    int il, changed;
    char		*parse_one_plural(char *);

    if (!(strchr(str,' ')))
	return make_mstring(parse_one_plural(str));

    words = explode_string(str, " ");
#ifdef OLD_EXPLODE
    il = 1;
#else
    for (il = 0 ; il < words->size ; il++)
	if (*(words->item[il].u.string))
	    break;
    il++;
#endif
    
    for (changed = 0; il < words->size; il++) 
    {
	if ((EQ(words->item[il].u.string,"of")) ||
	    (il + 1 == words->size))
	{
	    spl = parse_one_plural(words->item[il - 1].u.string);
	    if (spl != words->item[il - 1].u.string)
	    {
		stmp.type = T_STRING;
		stmp.string_type = STRING_MSTRING;
		stmp.u.string = make_mstring(spl);
		assign_svalue(&words->item[il - 1], &stmp);
		changed = 1;
		free_svalue(&stmp);
	    }
	} 
    }
    if (!changed)
    {
	free_vector(words);
	return make_mstring(str);
    }
    str = implode_string(words, " ");
    free_vector(words);
    return str;
}
    

/*
 * Function name: 	parse_one_plural
 * Description:		Change a noun in singularform to a noun
 *			in pluralform.
 * Arguments:		str: The sentence to change
 * Returns:		Word in plural form.
 */
char *
parse_one_plural(char *str)
{
    char 	ch, ch2, ch3;
    int 	sl;
    static char	pbuf[100];   /* Words > 100 letters? In Wales maybe... */
    
    sl = strlen(str) - 1; 
    if ((sl < 2) || (sl > 90))
	return str;

    ch = str[sl];
    ch2 = str[sl - 1];
    ch3 = str[sl - 2];
    (void)strcpy(pbuf, str); pbuf[sl] = 0;

    switch (ch)
    {
    case 'h':
	if (ch2 == 's' || ch2 == 'c')
	    return strcat(pbuf, "hes");
	/* FALLTHROUGH */
    case 'f':
	return strcat(pbuf, "ves");
    case 's':
	return strcat(pbuf, "ses");
    case 'x':
	if (EQ(str,"ox"))
	    return "oxen";
	else
	    return strcat(pbuf, "xes");
    case 'y':
	if ((ch2 != 'a' && ch2 != 'e' && ch2 != 'i' && ch2 != 'o' &&
	     ch2 != 'u') || (ch2 == 'u' && ch3 == 'q'))
	    return strcat(pbuf, "ies");
	/* FALLTHROUGH */
    case 'e':
	if (ch2 == 'f')
	{
	    pbuf[sl - 1] = 0;
	    return strcat(pbuf, "ves");
	}
    }
    
    if (EQ(str,"corpse")) return "corpses";
    if (EQ(str,"tooth")) return "teeth";
    if (EQ(str,"foot")) return "feet";
    if (EQ(str,"man")) return "men";
    if (EQ(str,"woman")) return "women";
    if (EQ(str,"child")) return "children";
    if (EQ(str,"goose")) return "geese";   
    if (EQ(str,"mouse")) return "mice";
    if (EQ(str,"deer")) return "deer";
    if (EQ(str,"moose")) return "moose";
    if (EQ(str,"sheep")) return "sheep";

    pbuf[sl] = ch;
    return strcat(pbuf, "s");
}
#endif
 
/*

   End of Parser

***************************************************************/

/* process_string
 *
 * Description:   Checks a string for the below occurences and replaces:
 *		  Fixes a call to a named function if the value is on the
 *                form: '@@function[:filename][|arg|arg]@@' Filename is
 *                optional.
 *		  Note that process_string does not recurse over returned
 *		  replacement values. If a function returns another function
 *		  description, that description will not be replaced.
 *                Example (added after reading TMI docs :-)
 *                "You are chased by @@query_name:/obj/monster#123@@ eastward."
 *                 is replaced by: 
 *                "You are chased by Orc eastward."
 *                               (if query_name in monster#123 returns "Orc")
 *                 
 *                Note that both object and arguments are optional.
 * Arguments:     str: A string containing text and function descriptions as
 *		  as described above.
 * Returns: 	  String containing the result of all replacements.
 */
char *
process_string(char *str, int other_ob)
{
    struct vector *vec;
    struct object *old_cur = current_object;
    int pr_start, il, changed;
    char *p2, *p3, *buf;
    char *process_part(char *, int);

    if (str == NULL || strstr(str,"@@") == NULL)
	return 0;

    /* This means we are called from notify_ in comm1 
       We must temporary set eff_user to backbone uid for
       security reasons.
    */
    if (!current_object)
    {
	current_object = command_giver;
    }
	
    vec = explode_string(str, "@@");

    /* If the first two chars is '@@' then vec[0] is a potential VBFC
    */
#ifdef OLD_EXPLODE
    pr_start = ((str[0] == '@') && (str[1] == '@')) ? 0 : 1;
#else
    pr_start = 1;
#endif

    /* Loop over each VBFC
       Terminating space is no longer allowed. 
       A VBFC must be terminated with '@@'.
    */
    for (changed = 0, il = pr_start; il < vec->size; il += 2) 
    {
	p2 = process_part(vec->item[il].u.string, other_ob);

	if (p2 == vec->item[il].u.string)  /* VBFC not resolved */
	{
	    /* Put back the leading and trailing '@@'. 
	       If some other VBFC resolves, this is needed.
	    */
	    p3 = xalloc(5 + strlen(p2));
	    (void)strcpy(p3, "@@"); 
	    (void)strcat(p3, p2);
	    (void)strcat(p3, "@@");
	    p2 = p3;
	}
	else
	    changed = 1;

	free_svalue(&vec->item[il]);
	vec->item[il].type = T_STRING;
	vec->item[il].string_type = STRING_MSTRING;
	vec->item[il].u.string = make_mstring(p2);
	free(p2);
    }

    if (changed)
	buf = implode_string(vec, "");
    else
	buf = 0;

    free_vector(vec);

    current_object = old_cur;

    return buf;
}

/*
 * Process a string holding exactly one 'value by function call'
 *
 * This function might return an alloced string or just the pointer
 * to the argument str. It is up to the caller to deallocate if
 * necessary. (This is done in process_string)
 */
char *
process_part(char *str, int other_ob)
{
    struct svalue *ret;
    struct svalue *process_value(char *, int);

    if (!(*str) || (str[0] < 'A') || (str[0] > 'z'))
	return str;

    ret = process_value(str, other_ob);

    if ((ret) && (ret->type == T_STRING))
	return string_copy(ret->u.string);
    else
	return str;
}

/*
 * Function name: process_value
 * Description:   Fixes a call to a named function on the form:
 *                'function[:filename][|arg|arg...|arg]' Filename is optional
 * Arguments:     str: Function as given above
 * Returns:       The value returned from the function call.
 */
struct svalue *
process_value(char *str, int other_ob)
{
    struct svalue *ret;
    char *func, *obj, *arg, *narg;
    int numargs;
    struct object *ob;

    if (!(*str) || (str[0] < 'A') || (str[0] > 'z'))
	return &const0;

    func = (char *) alloca(strlen(str) + 1);
    (void)strcpy(func, str);

    arg = strchr(func,'|'); if (arg) { *arg=0; arg++; }
    obj = strchr(func,':'); if (obj) { *obj=0; obj++; }

    /* Find the objectpointer
    */
    if (!obj) 
	ob = current_object;
    else
	ob = find_object2(obj);

    if (!ob)
    {
	return &const0;
    }

    /* Push all arguments as strings to the stack
    */
    for (numargs = 0; arg; arg = narg)
    {
	narg = strchr(arg,'|');
	if (narg) 
	    *narg = 0; 
	push_string(arg, STRING_MSTRING);
	numargs++;
	if (narg) 
	{
	    *narg = '|'; 
	    narg++;
	}
    }

    if (ob->flags & O_DESTRUCTED)
	error("object used by process_string/process_value destructed\n");
    
    if (other_ob)
    {
	extern char *pc;

	if (!vbfc_object)
	    error("No vbfc object.\n");
	push_control_stack(vbfc_object, vbfc_object->prog, 0);
	current_prog = vbfc_object->prog;
	pc = vbfc_object->prog->program;	/* XXX */
	inh_offset = current_prog->num_inherited;
	previous_ob = current_object;
	current_object = vbfc_object;
	csp->ext_call = 1;
	
    }
    
    /*
     * Apply the function and see if adequate answer is returned
     */
    ret = apply(func, ob, numargs, 1);

    if (other_ob)
	pop_control_stack();
    
    if (!ret)
	return &const0;
    else
	return ret;
}

/*
 * Function name: break_string
 * Description:   Breaks a continous string without newlines into a string
 *		  with newlines inserted at regular intervalls replacing spaces
 *		  Each newline separated string can be indented with a given
 *		  number of spaces.
 * Arguments:     str: Original message
 *		  width: The total maximum width of each line.
 *		  indent: (optional) How many spaces to indent with or 
 *                        indent string. (Max 1000 in indent)
 * Returns:       A string with newline separated strings
 */
char *
break_string(char *str, int width, struct svalue *indent)
{
    char 	*fstr, *istr;
    struct 	vector *lines;
    long long l;
    int il, nchar, space, indlen;


    if (!indent)
	istr = 0;

    else if (indent->type == T_NUMBER && indent->u.number >= 0 && indent->u.number < 1000)
    {
	l = indent->u.number;
	istr = xalloc((size_t)l + 1);
	for (il = 0; il < l; il++) 
	    istr[il] = ' ';
	istr[il] = 0;
    }
    else if (indent->type == T_STRING)
	istr = string_copy(indent->u.string);
    else
	return 0;

    if (width < 1)
	width = 1;

    if (istr)
    {
	indlen = strlen(istr);
	if (width <= indlen)
	    width = indlen + 1;
    }
    else
	indlen = 0;
    
    l = strlen(str);
    if (l == 0) {
        return make_mstring("");
    }

    /*
     * Split with newlines
     */
    space = -1;
    nchar = 0;
    l = strlen(str);
    fstr = str;
    for (il = 0; il < l; il++)
    {
	if (fstr[il] == ' ')
	    space = il;

	if ((il - nchar) >= (width - indlen) && space >= 0)
	{
            if (fstr == str)
                fstr = make_mstring(str);
	    fstr[space] = '\n';
	    nchar = space + 1;
	    space = -1;
	}
    }

    /*
     * Nothing or empty string as indentstring => Return direct
     */
    if (!indlen)
    {
	if (istr)
	    free(istr);
        if (fstr == str)
            return 0;
	return fstr;
    }

    /*
     * Explode into array 
     */
    lines = explode_string(fstr, "\n");
    space = fstr[strlen(fstr) - 1] == '\n';

    if (fstr != str)
        free_mstring(fstr);

    /*
     * Calculate size of the indented string
     */
    for (nchar = 0, il = 0; il < lines->size; il++)
    {
	nchar += indlen + strlen(lines->item[il].u.string) + 1;
    }
    
    fstr = allocate_mstring((size_t)nchar);
    fstr[0] = 0;

    for (il = 0; il < lines->size - 1; il++)
    {
	(void)strcat(fstr, istr);
	(void)strcat(fstr, lines->item[il].u.string);
	(void)strcat(fstr, "\n");
    }
    if (lines->size > 0) {
	(void)strcat(fstr, istr);
	(void)strcat(fstr, lines->item[il].u.string);
    }
    if (space)
	(void)strcat(fstr, "\n");
    free(istr);
    free_vector(lines);

    return fstr;

}
