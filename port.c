#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <limits.h>

#include "random.h"
#include "config.h"
#include "lint.h"

double current_time;
double alarm_time;

/*
 * This file defines things that may have to be changed when porting
 * LPmud to new environments. Hopefully, there are #ifdef's that will take
 * care of everything.
 */

/*
 * The function time() can't really be trusted to return an integer.
 * But this game uses the 'current_time', which is an integer number
 * of seconds. To make this more portable, the following functions
 * should be defined in such a way as to retrun the number of seconds since
 * some chosen year. The old behaviour of time(), is to return the number
 * of seconds since 1970.
 *
 * alarm_time must never move backwards.
 */

void 
set_current_time() 
{
    static double alarm_base_time, alarm_last_time;
    struct timeval tv;

    (void)gettimeofday(&tv, NULL);
    current_time = tv.tv_sec + tv.tv_usec * 1e-6;

    if (alarm_base_time == 0)
        alarm_base_time = current_time;

    alarm_time = current_time - alarm_base_time;
    if (alarm_time < alarm_last_time) {
        alarm_time = alarm_last_time;
        alarm_base_time = current_time - alarm_time;
    }
    alarm_last_time = alarm_time;
}

char *
time_string(time_t t)
{
    return ctime(&t);
}

