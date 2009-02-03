/*-
 * Copyright (c) 1996 Dave Richards <dave@synergy.org>
 * Copyright (c) 1996 Thorsten Lockert <tholo@sigmasoft.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * and exceptions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 4. The code can not be used by Gary Random, Random Communications, Inc.,
 *    the employees of Random Communications, Inc. or its subsidiaries,
 *    including Defiance MUD, without prior written permission from the
 *    authors.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <sys/types.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "hash.h"
#include "simulate.h"

int num_distinct_strings_shared;
long bytes_distinct_strings_shared;
long overhead_bytes_shared;

int num_distinct_strings_malloced;
long bytes_distinct_strings_malloced;
long overhead_bytes_malloced;

static long long search_len;
static long long num_str_searches;
static long allocd_bytes_malloced;
static int allocd_strings_malloced;
static long allocd_bytes_shared;
static int allocd_strings_shared;

static char *sstring_table[HTABLE_SIZE];

/* 1 G characters should be big enough */
#define MAX_STRING_SIZE (1<<30)

char *
allocate_mstring(size_t len)
{
    char *cp;
    if (len < 0 || len > MAX_STRING_SIZE)
	error("Illegal string size.\n"); 

    cp = (char *)xalloc(mstring_header + len + 1) + mstring_header;
#ifdef DEBUG
    mstring_magic(cp) = MSTRING_MAGIC;
#endif
    mstring_count(cp) = 1;
    mstring_len(cp) = len;
    allocd_strings_malloced++;
    allocd_bytes_malloced += mstring_header + len + 1;
    num_distinct_strings_malloced++;
    bytes_distinct_strings_malloced += len + 1;
    overhead_bytes_malloced += mstring_header;
    return cp;
}

char *
make_mstring(const char *cp)
{
    size_t len;
    char *xp;

    len = strlen(cp);
    xp = allocate_mstring(len);
    (void)memcpy(xp, cp, len + 1);
    return xp;
}

char *
reference_mstring(char *cp)
{
#ifdef DEBUG
    if (mstring_magic(cp) != MSTRING_MAGIC)
	fatal("Bad m-magic: %lx %x\n", mstring_magic(cp), MSTRING_MAGIC);
#endif

    if (mstring_count(cp) != 0)
	mstring_count(cp)++;

    allocd_strings_malloced++;
    allocd_bytes_malloced += mstring_header + mstring_len(cp) + 1;

    return cp;
}

void
free_mstring(char *cp)
{
#ifdef DEBUG
    if (mstring_magic(cp) != MSTRING_MAGIC)
	fatal("Bad m-magic: %lx %x\n", mstring_magic(cp), MSTRING_MAGIC);
#endif

    if (mstring_count(cp) != 0) {
	allocd_strings_malloced--;
	allocd_bytes_malloced -= mstring_header + mstring_len(cp) + 1;
	if (--mstring_count(cp) == 0)
	{
#ifdef DEBUG
	    mstring_magic(cp) = 0;
#endif
	    num_distinct_strings_malloced--;
	    bytes_distinct_strings_malloced -= mstring_len(cp) + 1;
	    overhead_bytes_malloced -= mstring_header;
	    free(cp - mstring_header);
	}
    }
}

char *
reference_sstring(char *cp)
{
#ifdef DEBUG
    if (sstring_magic(cp) != SSTRING_MAGIC)
	fatal("Bad s-magic: %lx %x\n", sstring_magic(cp), SSTRING_MAGIC);
#endif

    if (sstring_count(cp) != 0)
	sstring_count(cp)++;

    allocd_strings_shared++;
    allocd_bytes_shared += sstring_header + sstring_len(cp) + 1;

    return cp;
}

char *
find_sstring(char *cp)
{
    unsigned short hash;
    char *xp;

    hash = HASH_SSTRING(cp);
    xp = sstring_table[hash];
    num_str_searches++;
    while (xp != NULL)
    {
	search_len++;
#ifdef DEBUG
	if (sstring_magic(xp) != SSTRING_MAGIC)
	    fatal("Bad s-magic: %lx %x\n", sstring_magic(xp), SSTRING_MAGIC);
#endif
	if (xp == cp || (*xp == *cp && strcmp(xp, cp) == 0))
	{
	    if (sstring_prev(xp) != NULL) {
		sstring_next(sstring_prev(xp)) = sstring_next(xp);
		if (sstring_next(xp) != NULL)
		    sstring_prev(sstring_next(xp)) = sstring_prev(xp);
		sstring_prev(xp) = NULL;
		sstring_next(xp) = sstring_table[hash];
		sstring_prev(sstring_table[hash]) = xp;
		sstring_table[hash] = xp;
	    }
	    return xp;
	}
	xp = sstring_next(xp);
    }

    return NULL;
}

char *
make_sstring(const char *cp)
{
    size_t len;
    unsigned short hash;
    char *xp;

    hash = HASH_SSTRING(cp);
    xp = sstring_table[hash];
    num_str_searches++;
    while (xp != NULL)
    {
	search_len++;
#ifdef DEBUG
	if (sstring_magic(xp) != SSTRING_MAGIC)
	    fatal("Bad s-magic: %lx %x\n", sstring_magic(xp), SSTRING_MAGIC);
#endif
	if (xp == cp || (*xp == *cp && strcmp(xp, cp) == 0))
	{
	    if (sstring_prev(xp) != NULL) {
		sstring_next(sstring_prev(xp)) = sstring_next(xp);
		if (sstring_next(xp) != NULL)
		    sstring_prev(sstring_next(xp)) = sstring_prev(xp);
		sstring_prev(xp) = NULL;
		sstring_next(xp) = sstring_table[hash];
		sstring_prev(sstring_table[hash]) = xp;
		sstring_table[hash] = xp;
	    }
	    return reference_sstring(xp);
	}
	xp = sstring_next(xp);
    }
    len = strlen(cp);
    if (len < 0 || len > MAX_STRING_SIZE)
	error("Illegal string size.\n"); 

    xp = (char *)xalloc(sstring_header + len + 1) + sstring_header;
#ifdef DEBUG
    sstring_magic(xp) = SSTRING_MAGIC;
#endif
    sstring_prev(xp) = NULL;
    sstring_next(xp) = sstring_table[hash];
    if (sstring_next(xp) != NULL)
	sstring_prev(sstring_next(xp)) = xp;
    sstring_hash(xp) = hash;
    sstring_count(xp) = 1;
    sstring_len(xp) = len;
    (void)memcpy(xp, cp, len + 1);
    sstring_table[hash] = xp;
    num_distinct_strings_shared++;
    bytes_distinct_strings_shared += len + 1;
    overhead_bytes_shared += sstring_header;
    allocd_strings_shared++;
    allocd_bytes_shared += sstring_header + sstring_len(xp) + 1;

    return xp;
}

void
free_sstring(char *cp)
{
#ifdef DEBUG
    if (sstring_magic(cp) != SSTRING_MAGIC)
	fatal("Bad s-magic: %lx %x\n", sstring_magic(cp), SSTRING_MAGIC);
#endif

    if (sstring_count(cp) != 0) {
	allocd_strings_shared--;
	allocd_bytes_shared -= sstring_header + sstring_len(cp) + 1;

	if (--sstring_count(cp) == 0)
	{
	    if (sstring_prev(cp) != NULL)
		sstring_next(sstring_prev(cp)) = sstring_next(cp);
	    else
		sstring_table[sstring_hash(cp)] = sstring_next(cp);
	    if (sstring_next(cp) != NULL)
		sstring_prev(sstring_next(cp)) = sstring_prev(cp);
#ifdef DEBUG
	    sstring_magic(cp) = 0;
#endif
	    num_distinct_strings_shared--;
	    bytes_distinct_strings_shared -= sstring_len(cp) + 1;
	    overhead_bytes_shared -= sstring_header;
	    free(cp - sstring_header);
	}
    }
}

char *
multiply_string(char *str, long long factor)
{
    char *result;
    long long size, newsize, offset;

    if (factor <= 0 || (size = strlen(str)) == 0) {
	return make_mstring("");
    }

    if (factor > MAX_STRING_SIZE)
	error("Illegal string size.\n"); 

    newsize = size * factor;
    result = allocate_mstring(newsize);
    for (offset = 0; offset < newsize; offset += size) {
	strcpy(result + offset, str);
    }
    return result;
}


void 
add_string_status(char *debinf)
{
    int i, n;
    char *cp;
    int min = INT_MAX;
    int max = INT_MIN;
    long sum1 = 0;
    long sum2 = 0;
    double mean, stddev;

    /*
     * Compute the min, max, sum of n and sum of n^2.
     */
    for (i = 0; i < HTABLE_SIZE; i++)
    {
	n = 0;
	for (cp = sstring_table[i]; cp != NULL; cp = sstring_next(cp))
	    n++;
	if (min > n)
	    min = n;
	if (max < n)
	    max = n;
	sum1 += n;
	sum2 += n * n;
    }

    if (sum1 == 0)
    {
	min = 0;
	max = 0;
    }

    /*
     * Compute the mean.
     */
    mean = (double)sum1 / HTABLE_SIZE;

    /*
     * Compute the standard deviation.
     */
    stddev = sqrt(((double)sum2 -
	(((double)sum1 * (double)sum1) / HTABLE_SIZE)) / HTABLE_SIZE);

    (void)strcat(debinf, "\nShared string hash table:\n");
    (void)strcat(debinf, "-------------------------     Strings        Bytes\n");
    
    (void)sprintf(debinf + strlen(debinf), "Total asked for\t\t%12d %12ld\n",
	    allocd_strings_shared, allocd_bytes_shared);
    (void)sprintf(debinf + strlen(debinf), "Actually used\t\t%12d %12ld\n",
	    num_distinct_strings_shared,
	    bytes_distinct_strings_shared + overhead_bytes_shared);

    (void)sprintf(debinf + strlen(debinf),
	    "Space actually required/total string bytes %6.2f%%\n",
	    (bytes_distinct_strings_shared + overhead_bytes_shared)*100.0 /
	    (double)allocd_bytes_shared);

    (void)sprintf(debinf + strlen(debinf),
	    "Searches     : %12lld  Average search: %7.3f\n",
	    num_str_searches,
	    (double)search_len / (double)num_str_searches);

    (void)sprintf(debinf + strlen(debinf),
	    "Hash size    : %12d\n", HTABLE_SIZE);
    (void)sprintf(debinf + strlen(debinf),
	    "Minimum depth: %12d  Average depth : %7.3f\n",
	    min, mean);
    (void)sprintf(debinf + strlen(debinf),
	    "Maximum depth: %12d  Std. deviation: %7.3f\n",
	    max, stddev);

    (void)strcat(debinf, "\nMalloced string table:\n");
    (void)strcat(debinf, "----------------------\t     Strings        Bytes\n");
    
    (void)sprintf(debinf + strlen(debinf), "Total asked for\t\t%12d %12ld\n",
	    allocd_strings_malloced, allocd_bytes_malloced);
    (void)sprintf(debinf + strlen(debinf), "Actually used\t\t%12d %12ld\n",
	    num_distinct_strings_malloced,
	    bytes_distinct_strings_malloced + overhead_bytes_malloced);

    (void)sprintf(debinf + strlen(debinf),
	    "Space actually required/total string bytes %6.2f%%\n",
	    (bytes_distinct_strings_malloced + overhead_bytes_malloced)*100.0 /
	    (double)allocd_bytes_malloced);
}

#ifdef DEBUG
void
dump_sstrings(void)
{
    char *str;
    short len;
    FILE *fp;
    int i;

    if ((fp = fopen("SSTRING_DUMP", "w")) == NULL)
	return;
    for (i = 0; i < HTABLE_SIZE; i++) {
	str = sstring_table[i];
	while (str) {
	    if (sstring_magic(str) != SSTRING_MAGIC)
		fatal("Bad s-magic: %lx %x\n", sstring_magic(str), SSTRING_MAGIC);
	    len = sstring_len(str);
	    (void)fwrite(&len, sizeof(len), 1, fp);
	    (void)fwrite(str, sizeof(char), strlen(str) + 1, fp);
	    str = sstring_next(str);
	}
    }
    (void)fclose(fp);
}
#endif /* DEBUG */

#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void
remove_string_hash()
{
    int i;

    for (i = 0 ; i < HTABLE_SIZE ; i++)
	sstring_table[i] = NULL;
}
#endif /* DEALLOCATE_MEMORY_AT_SHUTDOWN */
