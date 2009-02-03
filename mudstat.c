/*
 * mudstat.c
 *
 * Keep hundreth of a second statistics 
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "lint.h"

#define MUDSTAT
#include "mudstat.h"

#ifdef RUSAGE			/* Defined in config.h */
#include <sys/resource.h>
#endif

int		num_move,		/* Number of move_object() */
    		num_mcall,		/* Number of calls to master ob */
		num_fileread,		/* Number of file reads */
		num_filewrite,		/* Number of file writes */
		num_compile;		/* Number of compiles */

static long	mch_t1;
static struct tms tms1;
static int	num_pr = 0,
		ms_eval_lim,    /* Low limit for logging eval cost */
		ms_time_lim,    /* Low limit for logging eval time */
		ms_active = 0;  /* True if logging active */

static void mark_millitime(void);

extern int s_flag;
			
/*
 * Set active / inactive and parameters
 */
void
mudstatus_set(int active, int eval_lim, int time_lim)
{
    float tifl = time_lim / 100.0;
    FILE *mstat;

    ms_active = active;

    if (active)
    {
	s_flag = 1;
	if ((mstat = fopen(MUDSTAT_FILE, "a")) != NULL)
	{
	    (void)fprintf(mstat,"\n%-35s (%5.2f) %8d\n",
		    "------ ON, Limits:", tifl, eval_lim);
	    (void)fclose(mstat);
	}
    }
    else
    {
	if (s_flag && ((mstat = fopen(MUDSTAT_FILE,"a")) != NULL))
	{
	    (void)fprintf(mstat,"\n%-35s\n", "------ OFF");
	    (void)fclose(mstat);
	}
	s_flag = 0;
    }
    ms_eval_lim = ((eval_lim >= 0) ? eval_lim : MUDSTAT_LOGEVAL);
    ms_time_lim = ((time_lim >= 0) ? time_lim : MUDSTAT_LOGTIME);
}

/*
 * Mark current millitime
 */ 
static void 
mark_millitime()
{
#ifdef RUSAGE
    static struct rusage rus;
    if (getrusage(RUSAGE_SELF, &rus) >= 0) 
    {                      
	/* Time in millisecs
	 */
	mch_t1 = rus.ru_utime.tv_sec * 1000 + rus.ru_utime.tv_usec / 1000 +
	    rus.ru_stime.tv_sec * 1000 + rus.ru_stime.tv_usec / 1000; 
    }
#endif

    (void)times(&tms1);
}

/*
 * Get millitime passed (actual real time)
 */
int 
get_millitime()
{
    float mtime;
    static struct tms tms2;

    (void)times(&tms2);
    mtime =  (tms2.tms_utime - tms1.tms_utime +
	      tms2.tms_stime - tms1.tms_stime) / 60.0;

    return (int)(mtime * 100.0);
}

#ifdef RUSAGE
/*
 * Get millitime passed (process time)
 */
int 
get_processtime()
{
    static struct rusage rus;
    long mch_t2;

    if (getrusage(RUSAGE_SELF, &rus) >= 0) 
    {                      
	/* Time in millisecs
	 */
	mch_t2 = rus.ru_utime.tv_sec * 1000 + rus.ru_utime.tv_usec / 1000 +
	    rus.ru_stime.tv_sec * 1000 + rus.ru_stime.tv_usec / 1000; 
    } else
	return 0;
    return (int)(mch_t2 - mch_t1);
}
#else
int get_processtime() { return 0; }
#endif

void 
reset_mudstatus()
{
    mark_millitime();
    num_move = 0;
    num_mcall = 0;
    num_fileread = 0;
    num_filewrite = 0;
    num_compile = 0;
}

void 
print_mudstatus(char *activity, int eval, int timem, int tiproc)
{
    FILE *mstat;
    float tifl, tifl2;

    if (!ms_active)
	return;

    if (timem < ms_time_lim && eval < ms_eval_lim)
	return;

    tifl = timem / 100.0;
    tifl2 = tiproc / 1000.0;

    if ((mstat = fopen(MUDSTAT_FILE,"a")) != NULL)
    {
	if (!num_pr)
	    (void)fprintf(mstat,"\n%-35s (%11s) %8s %3s %3s %3s %3s %3s\n",
			  "Activity", "time:cpu", "evalcost", "cp", "mc",
			  "rd", "wr", "mv");
	num_pr = (num_pr + 1) % 10;
	if (strlen(activity) < 35U)
	    (void)fprintf(mstat,"%-35s (%5.2f:%5.2f) %8d %3d %3d %3d %3d %3d\n",
			  activity, tifl, tifl2, eval, num_compile, num_mcall, 
			  num_fileread, num_filewrite, num_move);
	else
	    (void)fprintf(mstat,"%s\n%-35s (%5.2f:%5.2f) %8d %3d %3d %3d %3d %3d\n",
			  activity, "", tifl, tifl2, eval, num_compile, num_mcall, 
			  num_fileread, num_filewrite, num_move);
	    
	(void)fclose(mstat);
    }
}
