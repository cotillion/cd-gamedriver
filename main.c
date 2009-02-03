#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>

#include "config.h"
#include "lint.h"
#include "interpret.h"
#include "object.h"
#include "lex.h"
#include "exec.h"
#include "mudstat.h"
#include "comm1.h"
#include "simulate.h"
#include "main.h"
#include "backend.h"

extern int current_line;

int d_flag = 0;	/* Run with debug */
int t_flag = 0;	/* Disable heart beat and reset */
int e_flag = 0;	/* Load empty, without castles. */
int s_flag = 0; /* Make statistics and dump to /MUDstatistics */
int no_ip_demon = 0;
int unlimited = 0; /* Run without eval cost limits */
int comp_flag = 0; /* Trace compilations */
int warnobsoleteflag = 0;
int driver_mtime;

void init_signals(void);
void create_object(struct object *ob);
void init_call_out(void);

#ifdef YYDEBUG
extern int yydebug;
#endif

int port_number = PORTNO;
#ifdef CATCH_UDP_PORT
int udp_port = CATCH_UDP_PORT;
#endif
#ifdef SERVICE_PORT
int service_port = SERVICE_PORT;
#endif
char *reserved_area;	/* reserved for malloc() */
struct svalue const0, const1, constempty;
struct closure funcempty;

double consts[5];

extern struct object *vbfc_object;

extern struct object *master_ob, *auto_ob;

int 
main(int argc, char **argv)
{
    extern int game_is_being_shut_down;
    int i, new_mudlib = 0;
    char *p;
    struct svalue *ret;
    extern struct svalue catch_value;
    extern void init_cfuns(void);
    struct gdexception exception_frame;

    (void)setlinebuf(stdout);

    const0.type = T_NUMBER; const0.u.number = 0;
    const1.type = T_NUMBER; const1.u.number = 1;
    constempty.type = T_FUNCTION; constempty.u.func = &funcempty;
    funcempty.funtype = FUN_EMPTY;
    catch_value = const0;
    
    /*
     * Check that the definition of EXTRACT_UCHAR() is correct.
     */
    p = (char *)&i;
    *p = -10;
    if (EXTRACT_UCHAR(p) != 0x100 - 10)
    {
	(void)fprintf(stderr, "Bad definition of EXTRACT_UCHAR() in config.h.\n");
	exit(1);
    }
    set_current_time();
#ifdef PROFILE_LPC
    set_profile_timebase(60.0); /* One minute */
#endif

#ifdef DRAND48
    srand48((long)current_time);
#else
#ifdef RANDOM
    srandom(current_time);
#else
#error No random generator specified!\n
#endif /* RANDOM */
#endif /* DRAND48 */

#if RESERVED_SIZE > 0
    reserved_area = malloc(RESERVED_SIZE);
#endif
    init_tasks();
    query_load_av();
    init_num_args();
    init_machine();
    init_cfuns();

    /*
     * Set up the signal handling.
     */
    init_signals();

    /*
     * The flags are parsed twice !
     * The first time, we only search for the -m flag, which specifies
     * another mudlib, and the D-flags, so that they will be available
     * when compiling master.c.
     */
    for (i = 1; i < argc; i++)
    {
	if (atoi(argv[i]))
	    port_number = atoi(argv[i]);
	else if (argv[i][0] != '-')
	    continue;
	switch(argv[i][1])
	{
	case 'D':
	    if (argv[i][2]) { /* Amylaar : allow flags to be passed down to
				 the LPC preprocessor */
		struct lpc_predef_s *tmp;
		
		tmp = (struct lpc_predef_s *)
		    xalloc(sizeof(struct lpc_predef_s));
		if (!tmp)
		    fatal("xalloc failed\n");
		tmp->flag = string_copy(argv[i]+2);
		tmp->next = lpc_predefs;
		lpc_predefs = tmp;
		continue;
	    }
	    (void)fprintf(stderr, "Illegal flag syntax: %s\n", argv[i]);
	    exit(1);
	    /* NOTREACHED */
	case 'N':
	    no_ip_demon++; continue;
	case 'm':
	    if (chdir(argv[i]+2) == -1)
	    {
	        (void)fprintf(stderr, "Bad mudlib directory: %s\n", argv[i]+2);
		exit(1);
	    }
	    new_mudlib = 1;
	    break;
	}
    }

    if (!new_mudlib && chdir(MUD_LIB) == -1) {
        (void)fprintf(stderr, "Bad mudlib directory: %s\n", MUD_LIB);
	exit(1);
    }

    if (setjmp(exception_frame.e_context))
    {
	clear_state();
	add_message("Anomaly in the fabric of world space.\n");
    } 
    else
    {
	exception_frame.e_exception = NULL;
	exception_frame.e_catch = 0;
	exception = &exception_frame;
	auto_ob = 0;
	master_ob = 0;
	
	if ((auto_ob = load_object("secure/auto", 1, 0, 0)) != NULL)
	{
	    add_ref(auto_ob, "main");
	    auto_ob->prog->flags |= PRAGMA_RESIDENT;
	}

	get_simul_efun();
	master_ob = load_object("secure/master", 1, 0, 0);
	if (master_ob)
	{
	    /*
	     * Make sure master_ob is never made a dangling pointer.
	     * Look at apply_master_ob() for more details.
	     */
	    add_ref(master_ob, "main");
	    master_ob->prog->flags |= PRAGMA_RESIDENT;
            resolve_master_fkntab();
	    create_object(master_ob);
            load_parse_information();
	    clear_state();
	}
    }
    exception = NULL;
    if (auto_ob == 0) 
    {
	(void)fprintf(stderr, "The file secure/auto must be loadable.\n");
	exit(1);
    }
    if (master_ob == 0) 
    {
	(void)fprintf(stderr, "The file secure/master must be loadable.\n");
	exit(1);
    }
    set_inc_list(apply_master_ob(M_DEFINE_INCLUDE_DIRS, 0));
    {
	struct svalue* ret1;

	ret1 = apply_master_ob(M_PREDEF_DEFINES, 0);
	if (ret1 && ret1->type == T_POINTER)
	{
	    int ii;
	    struct lpc_predef_s *tmp;

	    for (ii = 0; ii < ret1->u.vec->size; ii++)
		if (ret1->u.vec->item[ii].type == T_STRING)
		{
		    tmp = (struct lpc_predef_s *)
			xalloc(sizeof(struct lpc_predef_s));
		    tmp->flag = string_copy(ret1->u.vec->item[ii].u.string);
		    tmp->next = lpc_predefs;
		    lpc_predefs = tmp;
		}
	}
    }
    for (i = 1; i < argc; i++)
    {
	if (atoi(argv[i]))
	    ;
	else if (argv[i][0] != '-')
	{
	    (void)fprintf(stderr, "Bad argument %s\n", argv[i]);
	    exit(1);
	}
	else 
	{
	    /*
	     * Look at flags. -m has already been tested.
	     */
	    switch(argv[i][1])
	    {
	    case 'f':
		push_string(argv[i]+2, STRING_MSTRING);
		(void)apply_master_ob(M_FLAG, 1);
		if (game_is_being_shut_down)
		{
		    (void)fprintf(stderr, "Shutdown by master object.\n");
		    exit(0);
		}
		continue;
	    case 'e':
		e_flag++; continue;
	    case 'O':
		warnobsoleteflag++; continue;
	    case 'D':
		continue;
	    case 'N':
		continue;
	    case 'm':
		continue;
	    case 'd':
		d_flag = atoi(argv[i] + 2);
		continue;
	    case 'c':
		comp_flag++; continue;
	    case 'l':
		unlimited++;
		continue;
	    case 't':
		t_flag++; continue;
	    case 'S':
		s_flag++; 
		mudstatus_set(1, -1, -1); /* Statistics, default limits */
		continue;
	    case 'u':
#ifdef CATCH_UDP_PORT
		udp_port = atoi (&argv[i][2]);
#endif
		continue;
	    case 'p':
#ifdef SERVICE_PORT
		service_port = atoi (&argv[i][2]);
#endif
		continue;
	    case 'y':
#ifdef YYDEBUG
		yydebug = 1;
#endif
		continue;
	    default:
		(void)fprintf(stderr, "Unknown flag: %s\n", argv[i]);
		exit(1);
	    }
	}
    }

    /*
     * See to it that the mud name is always defined in compiled files
     */
    ret = apply_master_ob(M_GET_MUD_NAME, 0);

    if (ret && ret->type == T_STRING)
    {
	struct lpc_predef_s *tmp;
		
	tmp = (struct lpc_predef_s *)
	    xalloc(sizeof(struct lpc_predef_s));
	if (!tmp) 
	    fatal("xalloc failed\n");
	tmp->flag = string_copy(ret->u.string);
	tmp->next = lpc_predefs;
	lpc_predefs = tmp;
    }

    ret = apply_master_ob(M_GET_VBFC_OBJECT, 0);
    if (ret && ret->type == T_OBJECT)
    {
	vbfc_object = ret->u.ob;
	INCREF(vbfc_object->ref);
    }
    else
	vbfc_object = 0;

    if (game_is_being_shut_down)
	exit(1);

    if (!t_flag)
	init_call_out();

    preload_objects(e_flag);
    (void)apply_master_ob(M_FINAL_BOOT, 0);
    

    mainloop();
    /* backend(); */

    return 0;
}

char *string_copy(str)
    char *str;
{
    char *p;

    p = xalloc(strlen(str)+1);
    (void)strcpy(p, str);
    return p;
}

/*VARARGS1*/
void
debug_message(char *fmt, ...)
{
    va_list argp;
    char *f;

    static FILE *fp = NULL;
    char deb[100];
    char name[100];

    if (fp == NULL) {
	(void)gethostname(name,sizeof name);
	if ((f = strchr(name, '.')) != NULL)
	    *f = '\0';
	(void)sprintf(deb,"%s.debug.log",name);
	fp = fopen(deb, "w");
	if (fp == NULL) {
	    perror(deb);
	    abort();
	}
    }

    va_start(argp, fmt);
    (void)vfprintf(fp, fmt, argp);
    /* LINTED: expression has null effect */
    va_end(argp);
    (void)fflush(fp);
}

void 
debug_message_svalue(struct svalue *v)
{
    if (v == 0)
    {
	debug_message("<NULL>");
	return;
    }
    switch(v->type)
    {
    case T_NUMBER:
	debug_message("%lld", v->u.number);
	return;
    case T_FLOAT:
	debug_message("%.18g", v->u.real);
	return;
    case T_STRING:
	debug_message("\"%s\"", v->u.string);
	return;
    case T_OBJECT:
	debug_message("OBJ(%s)", v->u.ob->name);
	return;
    case T_LVALUE:
	debug_message("Pointer to ");
	debug_message_svalue(v->u.lvalue);
	return;
    default:
	debug_message("<INVALID>\n");
	return;
    }
}

int slow_shut_down_to_do = 0;

#ifdef malloc
#undef malloc
#endif

void *
xalloc(unsigned int size)
{
    char *p;
    static int going_to_exit;

    if (going_to_exit)
	exit(3);
    if (size == 0)
	size = 1;
    p = (char *)malloc(size);
    if (p == 0)
    {
	if (reserved_area)
	{
	    free(reserved_area);
	    reserved_area = 0;
	    p = "Temporary out of MEMORY. Freeing reserve.\n";
	    (void)write(1, p, strlen(p));
	    slow_shut_down_to_do = 6;
	    return xalloc(size);	/* Try again */
	}
	going_to_exit = 1;
	p = "Totally out of MEMORY.\n";
	(void)write(1, p, strlen(p));
	(void)dump_trace(0);
	exit(2);
    }
    return p;
}
