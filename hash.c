#include <limits.h>
#include <string.h>

#include "random.h"
#include "siphash.h"

        
static unsigned char key[16];

/*
 * Initialize the hash with a random key to prevent collision attacks 
 */
void
init_hash() {
    int i = 0;
    while (i < sizeof(key)) {
        unsigned int rnd = secure_rand();

        for (int bit = 0; bit < sizeof(rnd); bit++) {
            unsigned char c = rnd & 0xff;
            rnd >>= CHAR_BIT;
            key[i] = c;
            i++;
        }
    }
}

unsigned long long
hash_string(const char *s) {
    return siphash(key, (unsigned char *)s, strlen(s));
}
