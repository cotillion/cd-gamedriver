/*
  debug.c

  This file keeps the debug() efun. All debug information and
  debug switches are managed from here.

*/
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>		/* sys/types.h and netinet/in.h are here to enable include of comm.h below */
#include <sys/stat.h>
/* #include <netinet/in.h> Included in comm.h below */
#include <memory.h>
#include <values.h>
#include <math.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "exec.h"
#include "interpret.h"
#include "udpsvc.h"
#include "call_out.h"

#ifdef RUSAGE			/* Defined in config.h */
#include <sys/resource.h>
extern int getrusage (int, struct rusage *);
#ifndef RUSAGE_SELF
#define RUSAGE_SELF	0
#endif
#endif

#if defined(PROFILE_LPC)
static struct vector *make_cpu_array (int,struct program *[]);
static struct vector *make_cpu_array2 (int,struct program *[]);
#endif


#include "object.h"
#include "instrs.h"
#include "patchlevel.h"
#include "comm.h"
#include "mapping.h"
#include "mudstat.h"
#include "bibopmalloc.h"
#include "simulate.h"
#include "comm1.h"
#include "super_snoop.h"

#include "inline_svalue.h"

#define OBJECT_DUMP_FILE "OBJECT_DUMP"
#define ALARM_DUMP_FILE  "ALARM_DUMP"
#define TRACE_CALLS_FILE  "TRACE_CALLS"

int call_warnings;

/*
 * The array below is the available subcommands to the debug() efun.
 *
 */
       			/*   Name		   Number   Params  */
static	char	*debc[] = { "index",		/* 0 */
			    "malloc", 		/* 1 */
			    "status", 		/* 2 */
			    "status tables",	/* 3 */
			    "mudstatus",	/* 4 	    on/off eval time */
			    "functionlist",	/* 5 	    object */
			    "rusage",		/* 6 */
			    "top_ten_cpu",	/* 7 */
			    "object_cpu",	/* 8 	    object */
			    "swap",		/* 9 	    object */
			    "version",		/* 10 */
			    "wizlist",		/* 11 	    wizname */
			    "trace",		/* 12 	    bitmask */
			    "traceprefix",	/* 13       pathstart */
			    "call_out_info",	/* 14       object */
			    "inherit_list",	/* 15	    object */
			    "load_average",	/* 16 */
			    "shutdown",		/* 17 */
			    "object_info",	/* 18 	    num object */
			    "opcdump",		/* 19 */
			    "send_udp",		/* 20       host, port, msg */
			    "mud_port",		/* 21       */
			    "udp_port",		/* 22       */
			    "set_wizard",	/* 23       player */
			    "ob_flags",	        /* 24       ob */
			    "get_variables",	/* 25       ob null/varname */
			    "get_eval_cost",	/* 26 */
                            "debug malloc",     /* 27 */
			    "getprofile",	/* 28	    object */
			    "get_avg_response",	/* 29 */
			    "destruct",         /* 30       object */
			    "destroy",          /* 31       object */
			    "update snoops",    /* 32 */
			    "call_warnings",    /* 33       on/off */
			    "dump_objects",     /* 34 */
			    "query_debug_ob",     /* 35 object */
			    "set_debug_ob",      /* 36 object flags */
			    "set_swap",         /* 37
			    ({min_mem, max_mem, min_time, max_time}) */
			    "query_swap",       /* 38 */
			    "set_debug_prog", /* 39  object */
			    "query_debug_prog", /* 40 object flags */
			    "functions",	/* 41 */
			    "inhibitcallouts",  /* 42       on/off */
			    "warnobsolete",     /* 43       on/off */
			    "shared_strings",	/* 44 */
                            "dump_alarms",      /* 45 */
                            "top_ten_cpu_avg",  /* 46 */
                            "object_cpu_avg",   /* 47 */
                            "getprofile_avg",   /* 48 */
                            "profile_timebase", /* 49 */
			    "trace_calls",      /* 50 */
			    "top_functions",    /* 51 */
			    0
			  };

extern struct vector *inherit_list (struct object *);
#ifdef FUNCDEBUG
void dumpfuncs(void);
#endif
#ifdef OPCPROF
void opcdump(void);
#endif

extern struct program *prog_list;
static double get_top_func_criteria(struct function *func, int criteria);
static void mem_variables(FILE *f, struct object *ob);
static void mem_incr(struct svalue *var);

struct svalue *
debug_command(char *debcmd, int argc, struct svalue *argv)
{
    static struct svalue retval;
    int dbnum, dbi, il;
    char buff[200];


    for (dbi = -1, dbnum = 0; debc[dbnum]; dbnum++)
    {
	if (strcmp(debcmd, debc[dbnum]) == 0)
	    dbi = dbnum;
    }
    if (dbi < 0)
    {
	retval.type = T_NUMBER;
	retval.u.number = 0;
	return &retval;
    }

    switch (dbi)
    {
    case 0: /* index */
	retval.type = T_POINTER;
	retval.u.vec = allocate_array(dbnum);
	for (il = 0; il < dbnum; il++)
	{
	    retval.u.vec->item[il].type = T_STRING;
	    retval.u.vec->item[il].string_type = STRING_CSTRING;
	    retval.u.vec->item[il].u.string = debc[il];
	}
	return &retval;
    case 1: /* malloc */
	retval.type = T_STRING;
	retval.string_type = STRING_MSTRING;
        char *data = dump_malloc_data();
	retval.u.string = make_sstring(data);
        free(data);
	return &retval;
    case 2: /* status */
    case 3: /* status tables */
	retval.type = T_STRING;
	retval.string_type = STRING_MSTRING;
	retval.u.string = (char *)get_gamedriver_info(debc[dbi]);
	return &retval;
    case 4: /* mudstatus on/off eval_lim time_lim */
	if (argc < 3 ||
	    argv[0].type != T_STRING ||
	    argv[1].type != T_NUMBER ||
	    argv[2].type != T_NUMBER)
	    break;
	if (strcmp(argv[0].u.string, "on") == 0)
	    mudstatus_set(1, argv[1].u.number, argv[2].u.number);
	else if (strcmp(argv[0].u.string, "off") == 0)
	    mudstatus_set(0, argv[1].u.number, argv[2].u.number);
	else
	    break;
	retval.type = T_NUMBER;
	retval.u.number = 1;
	return &retval;
    case 5: /* functionlist object */
	if (argc < 1 || argv[0].type != T_OBJECT)
	    break;
	retval.type = T_POINTER;
	retval.u.vec = allocate_array(argv[0].u.ob->prog->num_functions);
	for (il = 0; il < (int)argv[0].u.ob->prog->num_functions; il++)
	{
	    retval.u.vec->item[il].type = T_STRING;
	    retval.u.vec->item[il].string_type = STRING_SSTRING;
	    retval.u.vec->item[il].u.string =
		reference_sstring(argv[0].u.ob->prog->functions[il].name);
	}
	return &retval;
    case 6: /* rusage */
    {
#ifdef RUSAGE /* Only defined if we compile GD with RUSAGE */
	char buff[500];
	struct rusage rus;
	long utime, stime;
	long maxrss;

	if (getrusage(RUSAGE_SELF, &rus) < 0)
	    buff[0] = 0;
	else {
	    utime = rus.ru_utime.tv_sec * 1000 + rus.ru_utime.tv_usec / 1000;
	    stime = rus.ru_stime.tv_sec * 1000 + rus.ru_stime.tv_usec / 1000;
	    maxrss = rus.ru_maxrss;
	    (void)sprintf(buff, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
		    utime, stime, maxrss, rus.ru_ixrss, rus.ru_idrss,
		    rus.ru_isrss, rus.ru_minflt, rus.ru_majflt, rus.ru_nswap,
		    rus.ru_inblock, rus.ru_oublock, rus.ru_msgsnd,
		    rus.ru_msgrcv, rus.ru_nsignals, rus.ru_nvcsw,
		    rus.ru_nivcsw);
	}
	retval.type = T_STRING;
	retval.string_type = STRING_MSTRING;
	retval.u.string = make_mstring(buff);
	return &retval;
#else
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with RUSAGE flag.\n";
	return &retval;
#endif
    }

#if defined(PROFILE_LPC)
    case 7: /* top_ten_cpu */
    {
#define NUMBER_OF_TOP_TEN 100
	struct program *p[NUMBER_OF_TOP_TEN];
	struct vector *v;
	struct program *prog;
	int i, j;
	for(i = 0; i < NUMBER_OF_TOP_TEN; i++)
	    p[i] = (struct program *)0L;
	prog = prog_list;
	do
	{
	    for(i = NUMBER_OF_TOP_TEN-1; i >= 0; i--)
	    {
		if ( p[i] && (prog->cpu <= p[i]->cpu))
		    break;
	    }

	    if (i < (NUMBER_OF_TOP_TEN - 1))
		for (j = 0; j <= i; j++)
		    if (strcmp(p[j]->name,prog->name) == 0)
		    {
			i = NUMBER_OF_TOP_TEN-1;
			break;
		    }

	    if (i < (NUMBER_OF_TOP_TEN - 1))
	    {
		j = NUMBER_OF_TOP_TEN - 2;
		while(j > i)
		{
		    p[j + 1] = p[j];
		    j--;
		}
		p[i + 1] = prog;
	    }
	} while (prog_list != (prog = prog->next_all));
	v = make_cpu_array(NUMBER_OF_TOP_TEN, p);
	if (v)
	{
	    retval.type = T_POINTER;
	    retval.u.vec = v;
	    return &retval;
	}
	break;
#undef NUMBER_OF_TOP_TEN
    }
#else
    case 7:
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with PROFILE_LPC flag.\n";
	return &retval;
#endif
    case 8: /* object_cpu object */
    {
	long long c_num;

	if (argc && (argv[0].type == T_OBJECT))
	{
#if defined(PROFILE_LPC)
	    c_num = argv[0].u.ob->prog->cpu * 1e6;
#else
	    retval.type = T_STRING;
	    retval.string_type = STRING_CSTRING;
	    retval.u.string = "Only valid if GD compiled with PROFILE_LPC flag.\n";
	    return &retval;
#endif
	}
	else
	{
#ifdef RUSAGE
	    struct rusage rus;

	    if (getrusage(RUSAGE_SELF, &rus) < 0)
	    {
		c_num = -1;
	    }
	    else
	    {
		c_num =  (long long)rus.ru_utime.tv_sec * 1000000 +
				     rus.ru_utime.tv_usec +
				     (long long)rus.ru_stime.tv_sec * 1000000 +
				     rus.ru_stime.tv_usec;
	    }
#else
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with RUSAGE flag.\n";
	return &retval;
#endif
        }
	retval.type = T_NUMBER;
	retval.u.number = c_num;
	return &retval;
    }

    case 9:  /*	swap,		object 		*/
#if 0        /* can not swap while executing */
	if (argc && (argv[0].type == T_OBJECT))
	    (void)swap(argv[0].u.ob);
#endif
	retval = const1;
	return &retval;
    case 10: /*	version,		  	*/
    {
	char buff[64];
	(void)snprintf(buff, sizeof(buff), "%6.6s%02d %s %s", GAME_VERSION, PATCH_LEVEL, __DATE__, __TIME__);
	retval.type = T_STRING;
	retval.string_type = STRING_MSTRING;
	retval.u.string = make_mstring(buff);
	return &retval;
    }
    case 11: /* wizlist,  	wizname	 	*/
	/*
	 * Prints information, will be changed
         */
	retval = const1;
	return &retval;
    case 12: /* trace, 		bitmask		*/
    {
	int ot = -1;
	extern struct object *current_interactive;
	if (current_interactive && current_interactive->interactive)
	{
	    if (argc && (argv[0].type == T_NUMBER))
	    {
		ot = current_interactive->interactive->trace_level;
		current_interactive->interactive->trace_level = argv[0].u.number;
	    }
	}

	retval.type = T_NUMBER;
	retval.u.number = ot;
	return &retval;
    }
    case 13: /* traceprefix, 	pathstart	*/
    {
	char *old = 0;

	extern struct object *current_interactive;
	if (current_interactive && current_interactive->interactive)
	{
	    if (argc)
	    {
		old = current_interactive->interactive->trace_prefix;
		if (argv[0].type == T_STRING)
		{
		    current_interactive->interactive->trace_prefix =
			make_sstring(argv[0].u.string);
		}
		else
		    current_interactive->interactive->trace_prefix = 0;
	    }
	}

	if (old)
	{
	    retval.type = T_STRING;
	    retval.string_type = STRING_SSTRING;
	    retval.u.string = old;
	}
	else
	    retval = const0;

	return &retval;
    }
    case 14: /*	call_out_info,	  		*/
	{
	    extern struct vector *get_calls(struct object *);
	    if (argv[0].type != T_OBJECT)
		break;
	    retval.type = T_POINTER;
	    retval.u.vec =  get_calls(argv[0].u.ob);
	    return &retval;
	}
    case 15: /* inherit_list, 	object		*/
	if (argc && (argv[0].type == T_OBJECT))
	{
	    retval.type = T_POINTER;
	    retval.u.vec = inherit_list(argv[0].u.ob);
	    return &retval;
	}
	else
	{
	    retval = const0;
	    return &retval;
	}
    case 16: /*	load_average,	  		*/
	retval.type = T_STRING;
	retval.string_type = STRING_MSTRING;
	retval.u.string = make_mstring(query_load_av());
	return &retval;

    case 17: /*	shutdown,		  	*/
	startshutdowngame(0);
	retval = const1;
	return &retval;

    case 18: /* "object_info",	num object 	*/
    {
	struct object *ob;
	char db_buff[1024], tdb[200];
	int i;

	if (argc < 2 || argv[0].type != T_NUMBER || argv[1].type != T_OBJECT)
	    break;

	if (argv[0].u.number == 0)
	{
	    int flags;
	    struct object *obj2;

	    if ( argv[1].type != T_OBJECT)
		break;
	    ob = argv[1].u.ob;
	    flags = ob->flags;
	    (void)sprintf(db_buff,"O_ENABLE_COMMANDS : %s\nO_CLONE           : %s\nO_DESTRUCTED      : %s\nO_SWAPPED         : %s\nO_ONCE_INTERACTIVE: %s\nO_CREATED         : %s\n",
			flags&O_ENABLE_COMMANDS ?"TRUE":"FALSE",
			flags&O_CLONE           ?"TRUE":"FALSE",
			flags&O_DESTRUCTED      ?"TRUE":"FALSE",
			flags&O_SWAPPED          ?"TRUE":"FALSE",
			flags&O_ONCE_INTERACTIVE?"TRUE":"FALSE",
			flags&O_CREATED		?"TRUE":"FALSE");

	    (void)sprintf(tdb,"time_of_ref : %d\n", ob->time_of_ref);
	    (void)strcat(db_buff, tdb);
	    (void)sprintf(tdb,"ref         : %d\n", ob->ref);
	    (void)strcat(db_buff, tdb);
#ifdef DEBUG
	    (void)sprintf(tdb,"extra_ref   : %d\n", ob->extra_ref);
	    (void)strcat(db_buff, tdb);
#endif
	    (void)snprintf(tdb, sizeof(tdb), "name        : '%s'\n", ob->name);
	    (void)strcat(db_buff, tdb);
	    (void)snprintf(tdb, sizeof(tdb), "next_all    : OBJ(%s)\n",
			ob->next_all?ob->next_all->name: "NULL");
	    (void)strcat(db_buff, tdb);
	    if (obj_list == ob)
	    {
		(void)strcat(db_buff, "This object is the head of the object list.\n");
	    }

	    obj2 = obj_list;
	    i = 1;
	    do
		if (obj2->next_all == ob)
		{
		    (void)snprintf(tdb, sizeof(tdb), "Previous object in object list: OBJ(%s)\n",
			    obj2->name);
		    (void)strcat(db_buff, tdb);
		    (void)sprintf(tdb, "position in object list:%d\n",i);
		    (void)strcat(db_buff, tdb);

		}
	    while (obj_list != (obj2 = obj2->next_all));
	}
        else if (argv[0].u.number == 1)
        {
	    if (argv[1].type != T_OBJECT)
		break;
	    ob = argv[1].u.ob;

	    (void)sprintf(db_buff,"program ref's %d\n", ob->prog->ref);
	    (void)snprintf(tdb, sizeof(tdb), "Name %s\n", ob->prog->name);
	    (void)strcat(db_buff, tdb);
	    (void)sprintf(tdb,"program size %d\n", ob->prog->program_size);
	    (void)strcat(db_buff, tdb);
	    (void)sprintf(tdb, "num func's %u (%u) \n", ob->prog->num_functions
			,ob->prog->num_functions * (unsigned) sizeof(struct function));
	    (void)strcat(db_buff, tdb);
	    (void)sprintf(tdb,"sizeof rodata %d\n", ob->prog->rodata_size);
	    (void)strcat(db_buff, tdb);
	    (void)sprintf(tdb,"num vars %u (%u)\n", ob->prog->num_variables
			,ob->prog->num_variables * (unsigned) sizeof(struct variable));
	    (void)strcat(db_buff, tdb);
	    (void)sprintf(tdb,"num inherits %u (%u)\n", ob->prog->num_inherited
			,ob->prog->num_inherited * (unsigned) sizeof(struct inherit));
	    (void)strcat(db_buff, tdb);
	    (void)sprintf(tdb,"total size %d\n", ob->prog->total_size);
	    (void)strcat(db_buff, tdb);
	}
        else
	{
	    (void)sprintf(db_buff, "Bad number argument to object_info: %lld\n",
		    argv[0].u.number);
        }
	retval.type = T_STRING;
	retval.string_type = STRING_MSTRING;
	retval.u.string = make_mstring(db_buff);
	return &retval;
    }
    case 19: /* opcdump,	19	    */
    {
#ifdef OPCPROF
	opcdump();
	retval = const1;
	return &retval;
#else
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with OPCPROF flag.\n";
	return &retval;
#endif
    }
    case 20: /* send_udp,	20     		host, port, msg */
    {
#ifdef CATCH_UDP_PORT
	extern udpsvc_t *udpsvc;
#endif
	if (argc < 3 ||
	    argv[0].type != T_STRING ||
	    argv[1].type != T_NUMBER ||
	    argv[2].type != T_STRING)
	    break;
#ifdef CATCH_UDP_PORT
	tmp = udpsvc_send(udpsvc, argv[0].u.string, argv[1].u.number, argv[2].u.string);
	if (tmp)
	    retval = const1;
	else
#endif
	    retval = const0;
	return &retval;
    }
    case 21: /* mud_port,	21  */
    {
	extern int port_number;
	retval.type = T_NUMBER;
	retval.u.number = port_number;
	return &retval;
    }
    case 22: /* udp_port,	22  */
    {
#ifdef CATCH_UDP_PORT
	extern int udp_port;
	retval.u.number = udp_port;
#else
	retval.u.number = -1;
#endif
	retval.type = T_NUMBER;
	return &retval;
    }
    case 23: /* set_wizard, 	object		*/
	if (argc && (argv[0].type == T_OBJECT))
	{
	    retval = const1;
	    return &retval;
	}
	else
	{
	    retval = const0;
	    return &retval;
	}
    case 24: /* ob_flags,	24 ob  */
    {
	if (argc && (argv[0].type == T_OBJECT))
	{
	    retval.type = T_NUMBER;
	    retval.u.number = argv[0].u.ob->flags;
	    return &retval;
	}
	retval = const0;
	return &retval;
    }
    case 25: /* get_variables, 25       object NULL/string */
    {
	struct svalue get_variables(struct object *);
	struct svalue get_variable(struct object *, char *);

 	switch (argc)
 	{
 	case 1:
 	    if ( argv[0].type != T_OBJECT)
 	    {
 		retval = const0;
 		return &retval;
 	    }
 	    retval = get_variables(argv[0].u.ob);
 	    return &retval;
 	case 2:
 	    if ( argv[0].type != T_OBJECT || argv[1].type != T_STRING)
 	    {
 		retval = const0;
 		return &retval;
 	    }
 	    retval = get_variable(argv[0].u.ob, argv[1].u.string);
 	    return &retval;
 	case 3:
	    if ( argv[0].type == T_OBJECT && argv[1].type == T_STRING)
	    {
		retval = get_variable(argv[0].u.ob, argv[1].u.string);
		return &retval;
	    }
 	    if ( argv[0].type == T_OBJECT)
	    {
		retval = get_variables(argv[0].u.ob);
		return &retval;
	    }
	    retval = const0;
	    return &retval;
 	default:
 	    retval = const0;
 	    return &retval;

 	}
    }
    case 26: /* get_eval_cost,	26  */
    {
	extern int eval_cost;
	retval.type = T_NUMBER;
	retval.u.number = eval_cost;
	return &retval;
    }

    case 27: /* debug malloc, 27 */
    {
        retval = const1;
        return &retval;
    }
    case 28: /* getprofile, 28	object */
    {
        int format = 0;
#ifndef PROFILE_LPC
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with PROFILE_LPC flag.\n";
	return &retval;
#else
	if (argc < 1 || argv[0].type != T_OBJECT)
	    break;
        if (argc >= 2 && argv[1].type == T_NUMBER)
            format = argv[1].u.number;
        if (format == 0) {
	    retval.type = T_POINTER;
	    retval.u.vec = allocate_array(argv[0].u.ob->prog->num_functions);
	    for (il = 0; il < (int)argv[0].u.ob->prog->num_functions; il++)
	    {

	        (void)snprintf(buff, sizeof(buff), "%016lld:%020lld: %s",
		        (long long)argv[0].u.ob->prog->functions[il].num_calls,
		        (long long)(argv[0].u.ob->prog->functions[il].time_spent * 1e6),
		        argv[0].u.ob->prog->functions[il].name);
	        retval.u.vec->item[il].type = T_STRING;
	        retval.u.vec->item[il].string_type = STRING_MSTRING;
	        retval.u.vec->item[il].u.string = make_mstring(buff);
	    }
        } else if (format == 1) {
            retval.type = T_POINTER;
            retval.u.vec = allocate_array(argv[0].u.ob->prog->num_functions);
            double now = current_cpu();
            struct program *prog = argv[0].u.ob->prog;
	    for (il = 0; il < (int)prog->num_functions; il++)
	    {
                struct function *func = prog->functions + il;
	        struct vector *res = allocate_array(7);
                update_func_profile(func, now, 0.0, 0.0, 0);
                res->item[0].type = T_STRING;
                res->item[0].string_type = STRING_MSTRING;
                res->item[0].u.string = make_mstring(func->name);

                res->item[1].type = T_FLOAT;
                res->item[1].u.real = func->time_spent * 1e6;

                res->item[2].type = T_FLOAT;
                res->item[2].u.real = func->tot_time_spent * 1e6;

                res->item[3].type = T_FLOAT;
                res->item[3].u.real = func->num_calls;
                res->item[4].type = T_FLOAT;
                res->item[4].u.real = func->avg_time * 1e6;

                res->item[5].type = T_FLOAT;
                res->item[5].u.real = func->avg_tot_time * 1e6;

                res->item[6].type = T_FLOAT;
                res->item[6].u.real = func->avg_calls;

	        retval.u.vec->item[il].type = T_POINTER;
	        retval.u.vec->item[il].u.vec = res;
            }
        } else {
	    retval.type = T_STRING;
	    retval.string_type = STRING_CSTRING;
	    retval.u.string = "Unknown format.\n";
        }
	return &retval;
#endif
    }
    case 29: /* get_avg_response, 29 */
    {
	extern int get_msecs_response(int);
	extern int msr_point;
	int sum, num, tmp;

	if (msr_point >=0)
	{
	    sum = 0;
	    num = 0;
	    for (il = 0; il < 100; il++)
	    {
		if ((tmp = get_msecs_response(il)) >=0)
		{
		    sum += tmp;
		    num++;
		}
	    }
	    retval.type = T_NUMBER;
	    retval.u.number = (num > 0) ? sum / num : 0;
	    return &retval;
	}
	break;
    }
    case 30: /* destruct, 30 */
    case 31: /* destroy, 31 */
    {
	extern void destruct_object(struct object *);

	if (argc && argv[0].type == T_OBJECT &&
            !(argv[0].u.ob->flags & O_DESTRUCTED))
            destruct_object(argv[0].u.ob);
	break;
    }
    case 32: /* update snoops, 31 */
#ifdef SUPER_SNOOP
	update_snoop_file();
#else
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with SUPER_SNOOP flag.\n";
#endif
	break;
    case 33: /* call_warnings, int 0 = off, 1 = on */
	if (argc && (argv[0].type == T_STRING))
	{
	    if (strcmp(argv[0].u.string, "on") == 0)
		call_warnings++;
	    else
		call_warnings = call_warnings > 0 ? call_warnings - 1 : 0;
	    retval.type = T_NUMBER;
	    retval.u.number = call_warnings;
	    return &retval;
	}
	else
	{
	    retval.type = T_NUMBER;
	    retval.u.number = -1;
	    return &retval;
	}
    case 34: /* dump objects */
    {
	FILE *ufile;
	struct object *ob;

	if ((ufile = fopen(OBJECT_DUMP_FILE, "w")) == NULL)
	{
	    retval.type = T_NUMBER;
	    retval.u.number = -1;
	    return &retval;
	}

	fputs("Array (size), Mapping (size), String (size), Objs, Ints, Floats, Inventory, Callouts, Environment, Name\n", ufile);
	ob = obj_list;
	do
	{
	    mem_variables(ufile, ob);
	}
	while (obj_list != (ob = ob->next_all));
	(void)fclose(ufile);
	break;
    }
    case 35: /* query_debug_ob */
	if (!argc || argv[0].type != T_OBJECT)
	    break;
	retval.type = T_NUMBER;
	retval.u.number = argv[0].u.ob->debug_flags;
	return &retval;
    case 36: /* set_debug_ob */
	if (!argc || argv[0].type != T_OBJECT || argv[1].type != T_NUMBER)
	    break;
	retval.type = T_NUMBER;
	retval.u.number = argv[0].u.ob->debug_flags;
	argv[0].u.ob->debug_flags = argv[1].u.number;
	return &retval;

    case 37: /* set_swap */
      retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Obsolete function.\n";
	return &retval;
    case 38: /* query_swap */
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Obsolete function.\n";
	return &retval;
    case 39: /* query_debug_prog */
	if (!argc || argv[0].type != T_OBJECT)
	    break;
	retval.type = T_NUMBER;
	retval.u.number = argv[0].u.ob->prog->debug_flags;
	return &retval;
    case 40: /* set_debug_prog */
	if (argc < 2 || argv[0].type != T_OBJECT || argv[1].type != T_NUMBER)
	    break;
	retval.type = T_NUMBER;
	retval.u.number = argv[0].u.ob->prog->debug_flags;
	argv[0].u.ob->prog->debug_flags = argv[1].u.number;
	return &retval;
#ifdef FUNCDEBUG
    case 41:
	dumpfuncs();
	retval = const0;
	return &retval;
#endif
    case 42: /* inhibitcallouts */
	if (argc && (argv[0].type == T_STRING))
	{
	    extern int inhibitcallouts;
	    int old;

	    old = inhibitcallouts;
	    if (strcmp(argv[0].u.string, "on") == 0)
		inhibitcallouts = 1;
	    else
		inhibitcallouts = 0;
	    retval.type = T_NUMBER;
	    retval.u.number = old;
	    return &retval;
	}
	else
	{
	    retval.type = T_NUMBER;
	    retval.u.number = -1;
	    return &retval;
	}
    case 43: /* inhibitcallouts */
	if (argc && (argv[0].type == T_STRING))
	{
	    extern int warnobsoleteflag;
	    int old;

	    old = warnobsoleteflag;
	    if (strcmp(argv[0].u.string, "on") == 0)
		warnobsoleteflag = 1;
	    else
		warnobsoleteflag = 0;
	    retval.type = T_NUMBER;
	    retval.u.number = old;
	    return &retval;
	}
	else
	{
	    retval.type = T_NUMBER;
	    retval.u.number = -1;
	    return &retval;
	}
    case 44: /* shared_strings */
#ifdef DEBUG
	dump_sstrings();
	retval = const0;
#else
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with DEBUG flag.\n";
#endif
	return &retval;
    case 45: /* dump_alarms */
        {
            int c;
            FILE *ufile;

            if ((ufile = fopen(ALARM_DUMP_FILE, "w")) == NULL)
            {
                retval.type = T_NUMBER;
                retval.u.number = -1;
                return &retval;
            }

            c = dump_callouts(ufile);
            fclose(ufile);

            retval.type = T_NUMBER;
            retval.u.number = c;
            return &retval;
        }

#ifdef PROFILE_LPC
    case 46: /* top_ten_cpu */
    {
#define NUMBER_OF_TOP_TEN 100
	struct program *p[NUMBER_OF_TOP_TEN];
	struct program *prog;
	struct vector *v;
	int i, j;
        double now = current_cpu();

	for(i = 0; i < NUMBER_OF_TOP_TEN; i++)
	    p[i] = (struct program *)0L;
	prog = prog_list;
	do
	{
            update_prog_profile(prog, now, 0.0, 0.0);

	    for(i = NUMBER_OF_TOP_TEN-1; i >= 0; i--)
	    {
		if ( p[i] && (prog->cpu_avg <= p[i]->cpu_avg))
		    break;
	    }

	    if (i < (NUMBER_OF_TOP_TEN - 1))
		for (j = 0; j <= i; j++)
		    if (strcmp(p[j]->name,prog->name) == 0)
		    {
			i = NUMBER_OF_TOP_TEN-1;
			break;
		    }

	    if (i < (NUMBER_OF_TOP_TEN - 1))
	    {
		j = NUMBER_OF_TOP_TEN - 2;
		while(j > i)
		{
		    p[j + 1] = p[j];
		    j--;
		}
		p[i + 1] = prog;
	    }
	} while (prog_list != (prog = prog->next_all));
	v = make_cpu_array2(NUMBER_OF_TOP_TEN, p);
	if (v)
	{
	    retval.type = T_POINTER;
	    retval.u.vec = v;
	    return &retval;
	}
	break;
#undef NUMBER_OF_TOP_TEN
    }
#else
    case 46:
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with PROFILE_LPC flag.\n";
	return &retval;
#endif
    case 47: /* object_cpu_avg object */
    {

#if defined(PROFILE_LPC)
	if (argc < 1 || (argv[0].type != T_OBJECT))
            break;
        update_prog_profile(argv[0].u.ob->prog, current_cpu(), 0.0, 0.0);
	retval.type = T_FLOAT;
	retval.u.number =argv[0].u.ob->prog->cpu_avg * 1e6;
	return &retval;
#else
        retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with PROFILE_LPC flag.\n";
	return &retval;
#endif
    }
    case 48: /* getprofile_avg, 28	object */
    {
#if defined(PROFILE_LPC)
	if (argc < 1 || argv[0].type != T_OBJECT)
	    break;
	retval.type = T_POINTER;
	retval.u.vec = allocate_array(argv[0].u.ob->prog->num_functions);
        double now = current_cpu();
        struct program *prog = argv[0].u.ob->prog;
	for (il = 0; il < (int)prog->num_functions; il++)
	{
            struct function *func = prog->functions + il;
	    struct vector *res = allocate_array(3);
            update_func_profile(func, now, 0.0, 0.0, 0);
            res->item[0].type = T_STRING;
            res->item[0].string_type = STRING_MSTRING;
            res->item[0].u.string = make_mstring(func->name);

            res->item[1].type = T_FLOAT;
            res->item[1].u.real = func->avg_time * 1e6;

            res->item[2].type = T_FLOAT;
            res->item[2].u.real = func->avg_calls;

	    retval.u.vec->item[il].type = T_POINTER;
	    retval.u.vec->item[il].u.vec = res;
	}
	return &retval;
#else
	retval.type = T_STRING;
	retval.string_type = STRING_CSTRING;
	retval.u.string = "Only valid if GD compiled with PROFILE_LPC flag.\n";
	return &retval;
#endif
    }
    case 49: /* profile_timebase */
    {
#if defined(PROFILE_LPC)
        if (argc < 1) { /* Return current value */
            retval.type = T_FLOAT;
            retval.u.real = get_profile_timebase();
            return &retval;
        }
        /* Update using old timebase */
        double now = current_cpu();
        struct program *prog = prog_list;
        do
        {
            update_prog_profile(prog, now, 0.0, 0.0);
            for (int i = 0; i < prog->num_functions; i++)
            {
                struct function *func = prog->functions + i;
                update_func_profile(func, now, 0.0, 0.0, 0);
            }
        } while (prog_list != (prog = prog->next_all));
        /* Set the new value */

        if (argv[0].type == T_NUMBER && argv[0].u.number > 0)
            set_profile_timebase(argv[0].u.number);
        else if (argv[0].type == T_FLOAT && argv[0].u.real > 1e-3)
            set_profile_timebase(argv[0].u.real);
        else
            break;

        retval = const1;
        return &retval;
#else
        retval.type = T_STRING;
        retval.string_type = STRING_CSTRING;
        retval.u.string = "Only valid if GD compiled with PROFILE_LPC flag.\n";
        return &retval;
#endif
    }
    case 50:

    {
#ifdef PROFILE_LPC
	extern int trace_calls;
	extern FILE *trace_calls_file;

        if (argc < 1 || argv[0].type != T_NUMBER)
	    break;

	if (!trace_calls && argv[0].u.number) {
	    if ((trace_calls_file = fopen(TRACE_CALLS_FILE, "w")) == 0)
                break;
            setvbuf(trace_calls_file, 0, _IOFBF, 1<<20); /* Set a 1MB buffer */
	    trace_calls = 1;
	} else if (trace_calls && !argv[0].u.number) {
	    fclose(trace_calls_file);
	    trace_calls_file = 0;
	    trace_calls = 0;
	}
        retval = const1;
        return &retval;
#else
        retval.type = T_STRING;
        retval.string_type = STRING_CSTRING;
        retval.u.string = "Only valid if GD compiled with PROFILE_LPC.\n";
        return &retval;
#endif
    }
    case 51:

    {
#if defined(PROFILE_LPC)
	long long num_top, criteria, num_items = 0;
        double now = current_cpu(), crit_val;
	struct program *prog;
	struct {
	    struct program *prog;
	    double crit_val;
	    unsigned short func_index;
	} *result;
        if (argc < 2 ||
	    argv[0].type != T_NUMBER ||
	    argv[1].type != T_NUMBER)
	    break;
	num_top = argv[0].u.number;
	criteria = argv[1].u.number;
	if (num_top < 0 || num_top > 1000) {
	    retval.type = T_STRING;
	    retval.string_type = STRING_CSTRING;
	    retval.u.string = "The number of itmes must be >= 0 and <= 1000.";
	    break;
	}
	if (criteria < 0 || criteria > 9) {
	    retval.type = T_STRING;
	    retval.string_type = STRING_CSTRING;
	    retval.u.string = "The criteria must be >= 0 and <= 9.";
	    break;
	}
	if (num_top == 0) {
	    retval.type = T_POINTER;
	    retval.u.vec = allocate_array(0);
	    return &retval;
	}

	result = xalloc(sizeof(*result) * (num_top + 1));
	memset(result, 0, sizeof(*result) * (num_top + 1));

	prog = prog_list;
	do
	{
            update_prog_profile(prog, now, 0.0, 0.0);
            for (int i = 0; i < prog->num_functions; i++)
            {
                struct function *func = prog->functions + i;
                update_func_profile(func, now, 0.0, 0.0, 0);
		crit_val = get_top_func_criteria(func, criteria);

		if (num_items == num_top &&
		    result[num_items - 1].crit_val >= crit_val)
		    continue;
		if (num_items == 0 || (num_items < num_top &&
		    result[num_items - 1].crit_val >= crit_val)) {
		    result[num_items].prog = prog;
		    result[num_items].func_index = i;
		    result[num_items].crit_val = crit_val;
		    num_items++;
		} else {
		    int insert = num_items;
		    while (insert > 0 &&
			   result[insert - 1].crit_val < crit_val)
			insert--;
		    memmove(&result[insert + 1], &result[insert],
			    sizeof(*result) * (num_items - insert));
		    result[insert].prog = prog;
		    result[insert].func_index = i;
		    result[insert].crit_val = crit_val;
		    if (num_items < num_top)
			num_items++;
		}
            }
	} while ((prog = prog->next_all) != prog_list);

	retval.type = T_POINTER;
	retval.u.vec = allocate_array(num_items);
	for (int i = 0; i < num_items; i++) {
	    struct vector *val = allocate_array(9);
	    prog = result[i].prog;
	    struct function *func = &prog->functions[result[i].func_index];
	    crit_val = result[i].crit_val;

	    val->item[0].type = T_STRING;
	    val->item[0].string_type = STRING_MSTRING;
	    val->item[0].u.string = make_mstring(prog->name);

	    val->item[1].type = T_STRING;
	    val->item[1].string_type = STRING_SSTRING;
	    val->item[1].u.string = func->name;
	    reference_sstring(func->name);

	    val->item[2].type = T_FLOAT;
	    val->item[2].u.real = crit_val;

	    val->item[3].type = T_FLOAT;
	    val->item[3].u.real = func->time_spent;

	    val->item[4].type = T_FLOAT;
	    val->item[4].u.real = func->avg_time;

	    val->item[5].type = T_FLOAT;
	    val->item[5].u.real = func->tot_time_spent;

	    val->item[6].type = T_FLOAT;
	    val->item[6].u.real = func->avg_tot_time;

	    val->item[7].type = T_FLOAT;
	    val->item[7].u.real = func->num_calls;

	    val->item[8].type = T_FLOAT;
	    val->item[8].u.real = func->avg_calls;

	    retval.u.vec->item[i].type = T_POINTER;
	    retval.u.vec->item[i].u.vec = val;
	}
	free(result);
        return &retval;
#else
        retval.type = T_STRING;
        retval.string_type = STRING_CSTRING;
        retval.u.string = "Only valid if GD compiled with PROFILE_LPC.\n";
        return &retval;
#endif
	}
    }

    retval = const0;
    return &retval;
}
#if defined(PROFILE_LPC)
static double
get_top_func_criteria(struct function *func, int criteria)
{
    switch (criteria) {
      case 0:
	return func->time_spent;
      case 1:
	return func->avg_time;
      case 2:
	return func->tot_time_spent;
      case 3:
	return func->avg_tot_time;
      case 4:
	return func->num_calls;
      case 5:
	return func->avg_calls;
      case 6:
	return func->num_calls > 0 ? func->time_spent / func->num_calls : -MAXDOUBLE;
      case 7:
	return func->avg_calls > 1e-30 ? func->avg_time / func->avg_calls : -MAXDOUBLE;
      case 8:
	return func->num_calls > 0 ? func->tot_time_spent / func->num_calls : -MAXDOUBLE;
      case 9:
	return func->avg_calls > 1e-30 ? func->avg_tot_time / func->avg_calls : -MAXDOUBLE;

      default:
	return -MAXDOUBLE;
    }
}
static struct vector *
make_cpu_array(int i, struct program *prog[])
{
    int num;
    struct vector *ret;
    char buff[1024]; /* should REALLY be enough */

    if (i <= 0)
	return 0;
    ret = allocate_array(i);

    for(num = 0; num < i; num++)
    {
	(void)sprintf(buff, "%16lld:%s",
		      (long long)(prog ? prog[num]->cpu * 1e6: 0L),
		      prog ? prog[num]->name : "");
	free_svalue(&ret->item[num]);
	ret->item[num].type = T_STRING;
	ret->item[num].string_type = STRING_MSTRING;
	ret->item[num].u.string = make_mstring(buff);
    }
    return ret;
}
static struct vector *
make_cpu_array2(int i, struct program *prog[])
{
    int num;
    struct vector *ret;
    char buff[1024]; /* should REALLY be enough */

    if (i <= 0)
	return 0;
    ret = allocate_array(i);

    for(num = 0; num < i; num++)
    {
	(void)sprintf(buff, "%22.18g:%s",
		      (double)(prog ? prog[num]->cpu_avg * 1e6: 0L),
		      prog ? prog[num]->name : "");
	free_svalue(&ret->item[num]);
	ret->item[num].type = T_STRING;
	ret->item[num].string_type = STRING_MSTRING;
	ret->item[num].u.string = make_mstring(buff);
    }
    return ret;
}
#endif

struct svalue
get_variables(struct object *ob)
{
    int i, j;
    struct vector *names;
    struct vector *values;
    struct svalue res;
    int num_var;

    if (ob->flags & O_DESTRUCTED)
	return const0;


    if (!ob->variables)
	return const0;

    num_var = ob->prog->num_variables + ob->prog->inherit[ob->prog->num_inherited - 1]
	.variable_index_offset;

    names = allocate_array(num_var);
    values = allocate_array(num_var);

    for (j = ob->prog->num_inherited - 1; j >= 0; j--)
	if (!(ob->prog->inherit[j].type & TYPE_MOD_SECOND) &&
	    ob->prog->inherit[j].prog->num_variables > 0)
	{
	    for (i = 0; i < (int)ob->prog->inherit[j].prog->num_variables; i++)
	    {
		if (num_var == 0)
		    error("Wrong number of variables in object.\n");
		names->item[--num_var].type = T_STRING;
		names->item[num_var].string_type = STRING_SSTRING;
		names->item[num_var].u.string =
		    reference_sstring(ob->prog->inherit[j].prog->
				      variable_names[i].name);
		assign_svalue_no_free(&values->item[num_var],
				      &ob->variables[ob->prog->inherit[j]
						     .variable_index_offset + i]);
	    }
	}
    res.type = T_MAPPING;
    res.u.map = make_mapping(names, values);
    free_vector(names);
    free_vector(values);
    return res;
}

struct svalue
get_variable(struct object *ob, char *var_name)
{
    int i;
    struct svalue res = const0;

   if (ob->flags & O_DESTRUCTED)
	return res;

    if (!ob->variables)
	return const0;

    if ((i = find_status(ob->prog, var_name,0)) != -1)
	assign_svalue_no_free(&res, &ob->variables[i]);
    return res;
}

/* Using globals is a bit unclean. I should return and tidy up later */
static int mapping_elem, array_elem, string_size, num_string;
static int num_map, num_arr, num_num, num_ob, num_float, num_func;
static int function_size;

void mem_mapping(struct mapping *);
void mem_array(struct vector *);

static void
mem_incr(struct svalue *var)
{
    switch(var->type)
    {
    case T_FUNCTION:
	function_size += sizeof (struct closure);
	num_func++;
	if (var->u.func->funargs)
	    mem_array(var->u.func->funargs);
	break;
    case T_MAPPING:
	mapping_elem += var->u.map->size;
	num_map++;
	mem_mapping(var->u.map);
	break;
    case T_POINTER:
	array_elem += var->u.vec->size;
	num_arr++;
	mem_array(var->u.vec);
	break;
    case T_STRING:
	string_size += strlen(var->u.string);
	num_string++;
	break;
    case T_OBJECT:
	/* Check for destructed objects while we'r at it :) */
	if (var->u.ob->flags & O_DESTRUCTED)
	{
	    num_num++;
	    free_svalue(var);
	    break;
	}
	num_ob++;
	break;
    case T_FLOAT:
	num_float++;
	break;
    case T_NUMBER:
	num_num++;
	break;
    default:
	;
    }
}

void
mem_mapping(struct mapping *m)
{
    struct apair **p;
    int i;
    int size;

    if (m->size < 0)
	return;
    size = m->size;
    m->size = -1;
    for (i = 0 ; i < size ; i++)
    {
        for(p = &m->pairs[i]; *p; )
        {
	    /* Check for destructed objects while we'r at it :) */
	    if ((*p)->arg.type == T_OBJECT &&
		((*p)->arg.u.ob->flags & O_DESTRUCTED))
	    {
		struct apair *f;

		f = *p;
		*p = f->next;
		f->next = 0;
		m->card -= free_apairs(f);
		mapping_elem--;
	    }
	    else
	    {
		mem_incr(&(*p)->arg);
		mem_incr(&(*p)->val);
		p = &(*p)->next;
	    }
        }
    }
    m->size = size;
}

void
mem_array(struct vector *v)
{
    int i;
    int size;

    if (v->size < 0)
	return;
    size = v->size;
    v->size = -1;
    for (i = 0; i < size; i++)
	mem_incr(&v->item[i]);
    v->size = size;
}

static void
mem_variables(FILE *f, struct object *ob)
{
    int i;
    int num_var, num_inv;
    struct object *o;

    mapping_elem = 0;
    array_elem = 0;
    string_size = 0;
    num_string = 0;
    num_arr = 0;
    num_map = 0;
    num_ob = 0;
    num_num = 0;
    num_float = 0;

    mem_incr(&ob->auth);

    num_var = ob->prog->num_variables + ob->prog->inherit[ob->prog->num_inherited - 1].variable_index_offset;
    if (!ob->variables)
	num_var = 0;
    for (i = 0; i < num_var; i++)
    {
	mem_incr(&ob->variables[i]);
    }
    for (num_inv = 0, o = ob->contains; o;
	 num_inv++, o = o->next_inv) ;

    fprintf(f, "%6d %6d %6d %6d"
	    " %6d %6d %6d %6d %6d"
	    " %6d %6d %s %s\n",
	    num_arr, array_elem, num_map, mapping_elem,
	    num_string, string_size, num_ob, num_num, num_float,
	    num_inv, ob->num_callouts, ob->super ? ob->super->name : "(void)", ob->name);
}
