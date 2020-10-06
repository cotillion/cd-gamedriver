#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "lint.h"

#ifdef malloc
#undef malloc
#endif

extern char *reserved_area;
extern int slow_shut_down_to_do;

#define INITIAL_POOL    256

/*
 * Allocate a chunk of memory and keep track of it in a pool.
 */
void *
pool_alloc(struct allocation_pool *pool, size_t size) 
{

    if (!pool->size) {
        pool->allocations = malloc(sizeof(void *) * INITIAL_POOL);

        if (pool->allocations == NULL) {
            return NULL;
        }

        pool->size = INITIAL_POOL;
    } 

    if (pool->size == pool->used) {
        size_t new_size = pool->size * 2;
        void *new_allocations = realloc(pool->allocations, new_size * sizeof(void *));
        if (new_allocations == NULL) {
            return NULL;
        }

        pool->size = new_size;
        pool->allocations = new_allocations;
    }

    return pool->allocations[pool->used++] = xalloc(size);
} 

/* 
 * Clear all allocations in the pool and prepare it for reuse 
 */
void
pool_free(struct allocation_pool *pool) {
    
    if (!pool->size) {
        return;
    }

    for (size_t i = 0; i < pool->used; i++) {
        free(pool->allocations[i]);
    }

    free(pool->allocations);
    pool->size = 0;
    pool->used = 0;
    pool->allocations = NULL;
}



/*
 * xalloc is a malloc wrapper which keeps a pool of 'extra' memory
 
 * 
 *
 */ 
__attribute__((malloc)) 
void *
xalloc(size_t size)
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
            return xalloc(size);        /* Try again */
        }
        going_to_exit = 1;
        p = "Totally out of MEMORY.\n";
        (void)write(1, p, strlen(p));
        (void)dump_trace(0);
        exit(2);
    }
    return p;
}

