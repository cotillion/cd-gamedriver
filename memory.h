#ifndef _memory_h
#define _memory_h

#include <stddef.h>

struct allocation_pool {
    size_t size;
    size_t used;
    void **allocations;
};

#define EMPTY_ALLOCATION_POOL { 0, 0, NULL };

void *xalloc (size_t);
void *pool_alloc(struct allocation_pool *, size_t);
void *pool_track(struct allocation_pool *, void *);
void pool_free(struct allocation_pool *);

#endif
