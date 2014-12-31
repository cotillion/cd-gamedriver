#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <limits.h>
#include <unistd.h>

#include "random.h"
#include "config.h"

/* This file handles everything concerning the PRNG,
 * Currently it is JKISS by David Jones */

/* Public domain code for JKISS RNG */ 
static unsigned int x = 123456789, y = 987654321, z = 43219876, c = 6543217; /* Seed variables */ 
static unsigned int x_save, y_save, z_save, c_save;

unsigned int JKISS() 
{ 
    unsigned long long t; 

    x = 314527869 * x + 1234567; 
    y ^= y << 5; y ^= y >> 7; y ^= y << 22; 
    t = 4294584393ULL * z + c; c = t >> 32; z = t; 

    return x + y + z; 
} 

unsigned int 
secure_rand(void) 
{ 
    int fn; 
    unsigned int r; 

    fn = open("/dev/urandom", O_RDONLY); 
    if (fn == -1) 
        exit(-1); /* Failed! */ 

    if (read(fn, &r, 4) != 4) 
        exit(-1); /* Failed! */ 

    close(fn); 

    return r; 
} 

void 
init_random() 
{ 
    x = secure_rand(); 
    while (!(y = secure_rand())); /* y must not be zero! */ 
    z = secure_rand(); 

    /* We don’t really need to set c as well but let's anyway… */ 
    /* NOTE: offset c by 1 to avoid z=c=0 */ 
    c = secure_rand() % 698769068 + 1; /* Should be less than 698769069 */ 
} 

/* Called when random requires a new seed, clear_random_seed must be called
 * to restore the previous seed values.
 */
void
set_random_seed(unsigned int seed) {
    x_save = x; y_save = y; z_save = z; c_save = c;
    x = 123456789; y = seed; z = 43219876; c = 6543217;
}

void
clear_random_seed() {
    x = x_save; y = y_save; z = z_save; c = c_save;
}
/*
 * Return a random argument in the range 0 .. n-1.
 */
long long
random_number(long long n)
{
    if (n <= 0)
        return 0;
    return ((unsigned long long)JKISS() * 4294967296 + (unsigned long long)JKISS()) % n;
}

double
random_double()
{
    double x; 
    unsigned int a, b; 

    a = JKISS() >> 6; /* Upper 26 bits */ 
    b = JKISS() >> 5; /* Upper 27 bits */ 
    x = (a * 134217728.0 + b) / 9007199254740992.0; 
    return x; 
}
