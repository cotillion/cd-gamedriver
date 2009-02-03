#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include "config.h"
#include "lint.h"

double random_float(int, char*);

double current_time;
double alarm_time;

/*
 * This file defines things that may have to be changed when porting
 * LPmud to new environments. Hopefully, there are #ifdef's that will take
 * care of everything.
 */

#ifdef RANDOM
static unsigned long newstate[] = {
    3,
    0x9a319039, 0x32d9c024, 0x9b663182, 0x5da1f342,
    0x7449e56b, 0xbeb1dbb0, 0xab5c5918, 0x946554fd,
    0x8c2e680f, 0xeb3d799f, 0xb11ee0b7, 0x2d436b86,
    0xda672e2a, 0x1588ca88, 0xe369735d, 0x904f35f7,
    0xd7158fd6, 0x6fa6f051, 0x616e6b96, 0xac94efdc,
    0xde3b81e0, 0xdf0a6fb5, 0xf103bc02, 0x48f340fb,
    0x36413f93, 0xc622c298, 0xf5a42ab8, 0x8a88d77b,
    0xf5ad9d0e, 0x8999220b, 0x27fb47b9
};
#endif

/*
 * Return a random argument in the range 0 .. n-1.
 * If a new seed is given, apply that before computing the random number.
 */
long long
random_number(long long n, int seedlen, char *seed)
{
    return (long long)(random_float(seedlen, seed) * n);
}

double
random_float(int seedlen, char *seedarr)
{
    double retval;
#ifdef DRAND48
    unsigned short newseed[3];
    char *seedstart;
    int i;
    if (seedlen) {
        newseed[0] = 0;
        newseed[1] = 0;
        newseed[2] = 0;
        seedstart = (char *)newseed;
        for (i = 0; i < seedlen; i++) {
            seedstart[i % sizeof(newseed)] ^= seedarr[i];
        }
        retval = ((double)nrand48(newseed))/(double) (1ll<<31);
        retval = (retval + (double)nrand48(newseed))/(double) (1ll<<31);
    } else {
        retval = ((double)lrand48())/(double) (1ll<<31);
        retval = (retval + (double)lrand48())/(double) (1ll<<31);
    }
#else
#ifdef RANDOM
    char *oldstate;
    unsigned int i, seed; 
    if (seedllen) {
        seed = 0;
        for(i = 0; i < seedlen; i++) {
            seed = (seed << 8) ^ ((seed >> 24) & 0xff) ^ seedarr[i] & 0xff;
        }
        initstate(seed, (char *)newstate, 128);
        oldstate = (char*)setstate((char *)newstate);
    }
    retval = ((double)random())/(double) (1ll<<31);
    retval = ((retval + (double)random())/(double) (1ll<<31);
    if (seedlen) {
	setstate(oldstate);
    }
#else
#error No random generator specified!
#endif
#endif
    return retval;
}
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
