#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "random.h"
#include "config.h"

/* This file handles everything concerning the PRNG,
 * Currently it is Xorshift128+ */

/* State variables for Xorshift128+ */
static unsigned long long s[2];
static unsigned long long s_save[2];

unsigned long long xorshift128plus() {
    unsigned long long x = s[0];
    const unsigned long long y = s[1];
    s[0] = y;
    x ^= x << 23; // a
    s[1] = x ^ y ^ (x >> 17) ^ (y >> 26); // b, c
    return s[1] + y;
}

void init_random() {
    s[0] = ((unsigned long long)secure_rand() << 32) | secure_rand();
    s[1] = ((unsigned long long)secure_rand() << 32) | secure_rand();
}

/* Called when random requires a new seed, clear_random_seed must be called
 * to restore the previous seed values.
 */
void set_random_seed(unsigned int seed) {
    s_save[0] = s[0];
    s_save[1] = s[1];
    s[0] = 6364136223846793005ULL * (seed ^ (seed >> 30)) + 1;
    s[1] = 1442695040888963407ULL * (seed ^ (seed >> 27)) + 1;
}

void clear_random_seed() {
    s[0] = s_save[0];
    s[1] = s_save[1];
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


/*
 * Return a random argument in the range 0 .. n-1.
 */
long long
random_number(long long n)
{
    if (n <= 0)
        return 0;
    return xorshift128plus() % n;
}

double
random_double()
{
    unsigned long long r = xorshift128plus();
    return (double)r / (double)0xFFFFFFFFFFFFFFFFULL;
}
