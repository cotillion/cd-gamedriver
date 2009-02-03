/*
**
** This is a special purpose malloc for lpmud.
** It is geared towards the special needs of the game driver:
**   - most allocations (>98%) are of small objects (<100 byte)
**   - very few allocations (< 0.03%) are large (>64k)
**
** This has lead to a malloc using three different strategies:
** small, medium, and large.
** All allocation is based on pages (typical page size is 32K).
** Pages are allocated on pages boundaries.  This makes it possible
** to store information about a page and its objects just at the
** beginning of a page.  Given a pointer in the middle of a page
** the address is just masked to page boundary and the page information
** is available there.
**
** Small objects:
** Objects with a size smaller than a threshold will
** be allocated in BIBOP (big bag of pages) style.
** A page consists of some information about the page in the
** beginning followed by a bitmap indicating the usage
** of objects and an array of objects.  Within a page
** all objects have the same size.
** To get to the information given a pointer to an
** object you mask off the lower part of the address.
** This method gives a very small space overhead (<2 bits) per object.
**
** Medium objects:
** Medium sized objects are allocated within a page.
** All the pages are linked, and the objects are linked
** within a page.  All free space is linked into doubly
** linked lists.  There are a number of list headers for different
** sizes of the free object.  The allocation uses best fit
** among the headers, and first fit in the doubly linked list.
** Free objects are coalesced when being freed.
**
** Large objects:
** Large objects are given several consecutive pages.
** Large requests are always rounded to a multiple of
** pages.
**
** Free pages (from freeing large or medium requests) are kept
** on a free list where best fit is used to do allocation.
**
** The current tuning of the allocation parameters seems to indicate
** that the space overhead is around 15%.
*/

/* #define BIBOP_DEBUG */
/* #define FREESTAT */
/* #define PARANOIA */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include "config.h"
#include <math.h>

#include "lint.h"
#ifdef DEBUG
#include "mstring.h"
#include "simulate.h"
#endif

#define SMALL_SIZE 0x80		/* smaller than this is uses bibop */
#define PAGE_SIZE 0x20000	/* 128k pages MUST!! be a power of 2 */
#define SIZE_SLOP 0x80		/* don't split a free space if leftover is less than this */
#define NFREE 20		/* number of headers for medium blocks */

typedef double aligntype;
#define ALIGNMENT (sizeof(aligntype)) /* alignment requirement for this platform */
typedef unsigned long mapword;	/* should be the largest possible unsigned type */

#define BITS_PER_BYTE 8		/* bits per unit in sizeof */

#define CEIL(x) (((x)+ALIGNMENT-1) & ~(ALIGNMENT-1))
#define PAGEMASK (~(PAGE_SIZE-1))

#define PTR_SIZE (sizeof(void *))

#define BITS_PER_MAPWORD (sizeof(mapword) * BITS_PER_BYTE)

struct bibop {
    int objsize;		/* Size of the objects (rounded) */
    int objfree;		/* Number of free objects */
    int maxobjfree;		/* Maximum objects that fit on the page */
    mapword *freemap;		/* Bitmap for free objects */
    int nextfree;		/* (Cached) index to look for free object */
    int maxfree;		/* Max index into freemap */
    void *objs;			/* Pointer to first object */
};
#define BIBOPMAGIC 0x55AA5A01
static long long p_bibop, m_bibop, f_bibop;
static long long s_bibop;

struct medium {
    aligntype data;
};
#define ALLOCMAGIC 0x55AA5A02
static long long p_alloc, m_alloc, f_alloc;
static long long s_alloc;
#define OBJSIZE(cur) ((char *)cur->next - (char *)OBJ_TO_DATA(cur))

struct freeblk {
    struct freeblk *fwd, *bwd;
#ifdef FREESTAT
    struct freehead *head;
#endif
};
struct freehead {
    struct freeblk h;
    int maxsize;
    int nalloc, nreal, nsplit, nfree, njoin, njoin1, njoin2, ndrop, nwhy, nskip;
};
static struct freehead freehead[NFREE];
#define FDEL(p) { (p)->bwd->fwd = (p)->fwd; (p)->fwd->bwd = (p)->bwd; }
#define FINS(p, q) { (p)->fwd = (q)->fwd; (q)->fwd = (p); (p)->bwd = (q); (p)->fwd->bwd = (p); }

struct big {
    int npages;
    aligntype data;
};
#define BIGMAGIC 0x55AA5A03
static long long p_big, m_big, f_big;
static long long s_big;

struct descr {
    int magic;			/* magic number for different page types */
    struct descr *dnext;	/* next page in whatever list it happens to be in */
    union {
	struct bibop bibop;
	struct medium medium;
	struct big big;
    } u;
};


static struct descr *bibops[(SMALL_SIZE+ALIGNMENT-1)/ALIGNMENT]; /* start of bibop chains */

static struct descr *newbibop(unsigned int);

static struct descr *firstpage;

typedef uintptr_t PTR;	/* same size as a pointer */

static struct descr *freepage = 0;
static long long p_free;

struct block {
#ifdef BIBOP_DEBUG
    int bmagic;
#endif
    int busy;
    struct block *next;
    union {
	aligntype data;
	struct freeblk fb;
    } u;
};
#define DATA_TO_OBJ(p) ((struct block *)( (char *)p - (long)&((struct block *)0)->u.data ))
#define OBJ_TO_DATA(p) ((void *)&p->u.data)
#define BMAGIC 0x19930921

static char *nextpage, *firstsbrk;

static int initdone = 0;

char *dump_malloc_data(void);
static void bibop_init(void);

#define OFFSET(t,f) ((long) &(((t *)0)->f))

/* Insert pages on free list, coalescing pages if possible */
static void
insertfreepage(struct descr *p)
{
    struct descr **fp;

#ifdef BIBOP_DEBUG
    (void)fprintf(stderr, "freeing %d pages at %lx", p->u.big.npages, p);
#endif
    /* find position */
    for(fp = &freepage; *fp && p > *fp; fp = &(*fp)->dnext)
	;
    /* insert into chain */
    p->dnext = *fp;
    *fp = p;
    /* check for adjecent following page */
    if ((char *)p->dnext == (char *)p + p->u.big.npages * PAGE_SIZE) {
	p->u.big.npages += p->dnext->u.big.npages;
	p->dnext = p->dnext->dnext;
#ifdef BIBOP_DEBUG
    (void)fprintf(stderr, ", join with next block");
#endif
    }
    /* check for adjecent preceding page */
    if (fp != &freepage) {
	struct descr *q;
	q = (struct descr *)((char *)fp - OFFSET(struct descr, dnext));
	if ((char *)q->dnext == (char *)q + q->u.big.npages * PAGE_SIZE) {
	    q->u.big.npages += q->dnext->u.big.npages;
	    q->dnext = q->dnext->dnext;
#ifdef BIBOP_DEBUG
    (void)fprintf(stderr, ", join with previous block");
#endif
	}
    }
#ifdef BIBOP_DEBUG
    {
	struct descr *f;
	(void)fprintf(stderr, "\nfree page list (nextpage=%lx):\n", nextpage);
	for(f = freepage; f; f = f->dnext) {
	    (void)fprintf(stderr, "    %lx (%lx) %d\n", f, (char *)f + f->u.big.npages * PAGE_SIZE, f->u.big.npages);
	}
    }
#endif
}

/* Allocate n pages. */
static void *
pages(unsigned int n)
{
    void *r;

#ifdef BIBOP_DEBUG
    (void)fprintf(stderr, "allocating %d pages", n);
#endif

    if (freepage) {
	/* try to find something on the free list */
	/* use best fit, slower but better */
	struct descr **p, **last = 0, **best = 0, *b;
	for(p = &freepage; *p; last = p, p = &(*p)->dnext) {
	    if ((*p)->u.big.npages == n) {
		best = p;
		break;
	    } else if ((*p)->u.big.npages > n) {
		if (!best || (*p)->u.big.npages < (*best)->u.big.npages)
		    best = p;
	    }
	}
	if (!best) {
	    /* couldn't find anything, try extending last block */
	    if ((char *)(*last) + (*last)->u.big.npages * PAGE_SIZE == nextpage) {
		int k = n - (*last)->u.big.npages;
		r = sbrk(k*PAGE_SIZE);
		if (!r || (intptr_t)r == -1) {
		    (void)fprintf(stderr, "sbrk(%ld) failed %ld(0x%lx) with return value %p\n", 
			    (long) k*PAGE_SIZE, (long)(nextpage - firstsbrk),
			    (unsigned long)(nextpage - firstsbrk), r);
		    return 0;
		}
		nextpage += k*PAGE_SIZE;
		(*last)->u.big.npages += k;
		best = last;
		p_free += k;
#ifdef BIBOP_DEBUG
		(void)fprintf(stderr, ", extending %d", k);
#endif
	    } else
		goto notfound;
	}
#ifdef BIBOP_DEBUG
	(void)fprintf(stderr, ", from free list");
#endif
	/* best points at something usable */
	b = *best;
	r = b;
	if (b->u.big.npages > n) {
	    /* split the block */
	    *best = (struct descr *)((char *)b + n * PAGE_SIZE);
	    (*best)->magic = b->magic;
	    (*best)->u.big.npages = b->u.big.npages - n;
	    (*best)->dnext = b->dnext;
#ifdef BIBOP_DEBUG
	(void)fprintf(stderr, ", split it %d %d", b->u.big.npages, n);
#endif
	} else {
	    *best = b->dnext;	/* remove block */
	}
	p_free -= n;
#ifdef BIBOP_DEBUG
    {
	struct descr *f;
	(void)fprintf(stderr, "\nfree page list (nextpage=%lx):\n", nextpage);
	for(f = freepage; f; f = f->dnext) {
	    (void)fprintf(stderr, "    %lx (%lx) %d\n", f, (char *)f + f->u.big.npages * PAGE_SIZE, f->u.big.npages);
	}
    }
#endif
	return r;
    }

 notfound:
#ifdef BIBOP_DEBUG
    (void)fprintf(stderr, " fresh\n");
#endif
    r = sbrk(n*PAGE_SIZE);
    if (!r || (intptr_t)r == -1)
    {
	(void)fprintf(stderr, "sbrk failed %ld with return value %p\n", (long)(nextpage - firstsbrk), r);
	return 0;
    }
    if (!(nextpage - PAGE_SIZE <= (char *)r && (char *)r <= nextpage + PAGE_SIZE))
    {
	(void)fprintf(stderr, "bad sbrk %p %p\n", r,
		nextpage);
    }
    r = nextpage;
    nextpage += n*PAGE_SIZE;
    return r;
}

/* Insert a (medium) block into the appropriate free list */
static void
insfree(struct block *b, int how)
{
    int s = OBJSIZE(b);
    int i;

    for(i = 0; i < NFREE && freehead[i].maxsize <= s; i++)
	;
#ifdef PARANOIA
    if (i == NFREE) {
	(void)fprintf(stderr, "insfree %d\n", s);
	abort();
    }
#endif
    FINS(&b->u.fb, &freehead[i].h);
#ifdef FREESTAT
    if (how == 1) freehead[i].nsplit++;
    if (how == 2) freehead[i].nfree++;
    if (how == 3) freehead[i].njoin++;
    b->u.fb.head = &freehead[i];
#elif defined(lint)
    how--;
#endif
}

/* Initialize a new medium page */
static void
init_allocpage(struct descr *p)
{
    struct block *first, *last;

    p->magic = ALLOCMAGIC;
    p->dnext = 0;
    first = (struct block *)&p->u.medium.data;
    last = (struct block *)((char *)p + PAGE_SIZE - sizeof(struct block));
#ifdef BIBOP_DEBUG
    first->bmagic = BMAGIC;
    last->bmagic = BMAGIC;
#endif    
    first->busy = 0;
    first->next = last;
    last->busy = 0;
    last->next = 0;
    insfree(first, 0);
}

/* Do a non-small allocation */
static void *
bigmalloc(unsigned int size)
{
    struct descr *p;
    struct block *cur, *next;
    int cursize, i;

    if (size > PAGE_SIZE - sizeof(struct descr) - ALIGNMENT - PTR_SIZE) {
	/* really big! */
	unsigned int n = (size + sizeof(struct descr) + PAGE_SIZE - 1) / PAGE_SIZE;
	struct descr *bigp = pages(n);
	if (!bigp)
	    return 0;
	p_big += n;
	bigp->magic = BIGMAGIC;
	bigp->u.big.npages = n;
	m_big++;
	s_big += size;
	return (void *)&bigp->u.big.data;
    }
	
    /* find first block which is big enough */
    for(i = 0; i < NFREE; i++) {
	if (freehead[i].maxsize > size) {
	    struct freeblk *fp;
	    for(fp = freehead[i].h.fwd; fp != &freehead[i].h; fp = fp->fwd) {
		cur = DATA_TO_OBJ(fp);
		cursize = OBJSIZE(cur);
		if (cursize >= size) {
		    p = (struct descr *)((PTR)cur & PAGEMASK);
		    goto found;
		}
	    }
	    freehead[i].nskip++;
	}
    }

    /* No page had a block that was big enough, get a new one. */
    p = pages(1);
    if (!p)
	return 0;
    p_alloc++;
    init_allocpage(p);
    p->dnext = firstpage;
    firstpage = p;
    cur = (struct block *)&p->u.medium.data;
    cursize = OBJSIZE(cur);
 found:
#ifdef BIBOP_DEBUG
    if (cur->bmagic != BMAGIC) {
	(void)fprintf(stderr, "bad magic 2\n");
	abort();
    }
#endif
    FDEL(&cur->u.fb);			/* remove from free lists */
#ifdef FREESTAT
    cur->u.fb.head->nalloc++;
    {
	int i;
	for(i = 0; i < NFREE && freehead[i].maxsize < size; i++)
	    ;
#ifdef PARANOIA
	if (i == NFREE)
	    abort();
#endif
	freehead[i].nreal++;
	if (cur == (struct block *)&p->u.medium.data && cur->next->next == 0)
	    freehead[i].nwhy++;
    }
#endif
    if (size + SIZE_SLOP < cursize) {
	/* we have to split the current block */
	next = (struct block *)((char *)cur + size + sizeof(struct block));
#ifdef BIBOP_DEBUG
	next->bmagic = BMAGIC;
#endif
	next->busy = 0;
#ifdef PARANOIA
	if ((struct descr *)((PTR)cur->next & PAGEMASK) != p) 
	    abort();
#endif
	next->next = cur->next;
	cur->next = next;
#ifdef PARANOIA
	if ((struct descr *)((PTR)cur->next & PAGEMASK) != p) 
	    abort();
#endif
	insfree(next, 1);		/* insert second half */
    }
    cur->busy = 1;
    m_alloc++;
    s_alloc += size;
    return (void *)&cur->u.data;
}

/* Table of the bit number of the last bit set in a byte. */
static int flsb[] = {
-1,0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 
};

#ifdef DEBUG
static void
missaligned_free(void *ptr, void *should_be)
{
    if (mstring_magic(should_be + mstring_header) == MSTRING_MAGIC)
	fatal("freeing m-magic string: %s\n", (char *)should_be + mstring_header);
    if (sstring_magic(should_be + sstring_header) == SSTRING_MAGIC)
	fatal("freeing s-magic string: %s\n", (char *)should_be + sstring_header);
    fatal("Freeing different pointer than malloced is %p should be %p.\n", ptr, should_be);
    
}
#endif

/* External interface */
void *
malloc(size_t size)
{
    register struct descr *bp;
    register int i, j;
    register mapword m;

    size = CEIL(size);
    if (!initdone)
	bibop_init();
    if (size >= SMALL_SIZE)
	return bigmalloc(size);
    for(bp = bibops[size/ALIGNMENT - 1]; bp && bp->u.bibop.objfree == 0; bp = bp->dnext)
	;
    if (!bp) {
	/* Add a new page */
	bp = newbibop(size);
	if (!bp)
	    return 0;
	bp->dnext = bibops[size/ALIGNMENT - 1];
	bibops[size/ALIGNMENT - 1] = bp;
    }
    /* locate word with free block */
    for(i = bp->u.bibop.nextfree; ;) {
	m = bp->u.bibop.freemap[i];
	if (m)
	    break;
	if (++i >= bp->u.bibop.maxfree)
	    i = 0;
#ifdef PARANOIA
	if (i == bp->u.bibop.nextfree) {
	    /* Help!  We've wrapped around without finding the promised free block! */
	    (void)fprintf(stderr, "bmalloc wrapped around, size = %ld\n", (long)size);
	    abort();
	}
#endif
    }
    bp->u.bibop.nextfree = i;
    /* locate bit within the word */
#define ONES (~(mapword)0)
    j = 0;
    if ((m & (ONES >> (BITS_PER_MAPWORD/2))) == 0) 
	j += BITS_PER_MAPWORD/2, m >>= BITS_PER_MAPWORD/2; 
    else 
	m &= (ONES >> (BITS_PER_MAPWORD/2));
    if ((m & (ONES >> (3*BITS_PER_MAPWORD/4))) == 0) 
	j += BITS_PER_MAPWORD/4, m >>= BITS_PER_MAPWORD/4; 
    else 
	m &= (ONES >> (3*BITS_PER_MAPWORD/4));
    if ((m & (ONES >> (7*BITS_PER_MAPWORD/8))) == 0) 
	j += BITS_PER_MAPWORD/8, m >>= BITS_PER_MAPWORD/8; 
    else 
	m &= (ONES >> (7*BITS_PER_MAPWORD/8));
    /* We're down to at most 8 bits now (if there are <= 64 bits in a word).
       Use a table to look up the last bit set.
    */
    j += flsb[(int)m];
    bp->u.bibop.freemap[i] &= ~ ((mapword)1<<j);	/* mark as allocated */
    bp->u.bibop.objfree--;		/* one less free */
    m_bibop++;
    s_bibop += size;
    return (void *) ((char *)bp->u.bibop.objs + (i * BITS_PER_MAPWORD + j) * size);
}

/* External interface */
void *
calloc(size_t nmemb, size_t size)
{
    void *p;

    p = malloc(nmemb * size);
    if (p != NULL)
	memset(p, '\0', nmemb * size);
    return p;
}

/* External interface */
void
free(void *ptr)
{
    register struct descr *bp;
    register unsigned int offs;
    int i, j;

    if (!ptr)
	return;

    if (!initdone)
	return;
    bp = (struct descr *)((PTR)ptr & PAGEMASK);
    if (bp->magic == BIGMAGIC) {
	/* freeing really big block, stuff all pages on free list */
	int n = bp->u.big.npages;
#ifdef DEBUG
        if (ptr != &bp->u.big.data)
            missaligned_free(ptr, &bp->u.big.data);
#endif

	insertfreepage(bp);
	f_big++;
	p_big -= n;
	p_free += n;
    } else if (bp->magic == ALLOCMAGIC) {
	/* freeing part of a page */
	struct block *this, *cur, *next;

	this = DATA_TO_OBJ(ptr);
#ifdef DEBUG
        if (ptr != &this->u.data)
            missaligned_free(ptr, &this->u.data);
#endif
            
#ifdef BIBOP_DEBUG
	if (DATA_TO_OBJ(ptr)->bmagic != BMAGIC) {
	    (void)fprintf(stderr, "bad magic 3\n");
	    abort();
	}
#endif
#ifdef PARANOIA
	if (!this->busy)
	    abort();
#endif
	this->busy = 0;
	insfree(this, 2);
	/* coalescing is tedious since we don't have back pointers */
	for(cur = (struct block *)&bp->u.medium.data; cur->next; cur = cur->next)
	    if (cur == this || (cur->next == this && !cur->busy))
		break;
#ifdef PARANOIA
	if (!cur->next || cur->busy)
	    abort();
#endif
	/* coalesce free blocks (max 3) */
	for(;;) {
	    next = cur->next;
#ifdef BIBOP_DEBUG
	    if (next->bmagic != BMAGIC) {
		(void)fprintf(stderr, "bad magic 1\n");
		abort();
	    }
#endif
	    if (!next->next || next->busy)
		break;
	    cur->next = next->next;
#ifdef BIBOP_DEBUG
	    next->bmagic = 0;
#endif
#ifdef PARANOIA
	    if ((struct descr *)((PTR)cur->next & PAGEMASK) != bp)
		abort();
#endif
#ifdef FREESTAT
	    cur->u.fb.head->njoin1++;
	    next->u.fb.head->njoin2++;
#endif
	    FDEL(&cur->u.fb);
	    FDEL(&next->u.fb);
	    insfree(cur, 3);
	}

	if (((struct block *)&bp->u.medium.data)->next->next == 0) {
	    /* The whole page is free now, move it to free list */
	    struct descr **p;
	    struct block *blk = (struct block *)&bp->u.medium.data;
	    FDEL(&blk->u.fb);
#ifdef FREESTAT
	    blk->u.fb.head->ndrop++;
#endif
	    for(p = &firstpage; *p != bp; p = &(*p)->dnext)
		;
	    *p = (*p)->dnext;
	    bp->u.big.npages = 1;
	    insertfreepage(bp);
	    p_free++;
	    p_alloc--;
	}
	f_alloc++;
    } else if (bp->magic == BIBOPMAGIC) {
	offs = ((char *)ptr - (char *)bp->u.bibop.objs) / bp->u.bibop.objsize; /* block offset from first block */
	i = offs / BITS_PER_MAPWORD;
	j = offs % BITS_PER_MAPWORD;
#ifdef DEBUG
        if (ptr != (char *)bp->u.bibop.objs + (offs * bp->u.bibop.objsize))
            missaligned_free(ptr, (char *)bp->u.bibop.objs + (offs * bp->u.bibop.objsize));
#endif
#ifdef PARANOIA
	if (bp->u.bibop.freemap[i] & (mapword)1<<j)
	    abort();
#endif
	bp->u.bibop.freemap[i] |= (mapword)1<<j;	/* mark as free */
	bp->u.bibop.nextfree = i;	/* we have a free block here */
	bp->u.bibop.objfree++;		/* one more free */
	f_bibop++;
    } else {
	/* Happens when freeing prematurely allocated objects. */
	(void)fprintf(stderr, "Warning, bad magic number in free %x (%p)\n",
		bp->magic, ptr);
    }
}

/* External interface */
void
cfree(void *ptr)
{
    free(ptr);
}

/* External interface */
void *
realloc(void *old, size_t size)
{
    struct descr *bp;
    size_t osize;
    void *new;
    
    if (!old)
	return malloc(size);
    bp = (struct descr *)((PTR)old & PAGEMASK);
    switch (bp->magic)
    {
    case BIGMAGIC:
	osize = bp->u.big.npages * PAGE_SIZE;
	break;
    case ALLOCMAGIC:
	osize = OBJSIZE(DATA_TO_OBJ(old));
	break;
    case BIBOPMAGIC:
	osize = bp->u.bibop.objsize;
	break;
    default:
	(void)fprintf(stderr, "Bad magic number in realloc %x\n", bp->magic);
	osize = 0;
	break;
    }
    if (osize > size)
	osize = size;
    else if (osize == size)
	return old;

    /* Always assume the worst and allocate&copy */
    new = malloc(size);
    if (!new)
	return 0;
    
    (void)memcpy(new, old, osize);

    free(old);
    return new;
}

/* Allocate and initialize a new bibop page. */
static struct descr *
newbibop(size)
unsigned int size;
{
    struct descr *bp;
    int nobj, nmap, nnmap;
    int i, j;

/*  (void)fprintf(stderr, "newbibop %d\n", size);*/
    bp = pages(1);
    if (!bp)
	return 0;
    p_bibop++;
    /* I'm to lazy to figure out the exact formula.  Iterate at runtime instead. */
    for(nmap = 0, nnmap = -1; nmap != nnmap;) {
	nobj = (PAGE_SIZE - sizeof(struct descr) - nmap * sizeof(mapword) - 2 * ALIGNMENT) / size;
	nnmap = nmap;
	nmap = (nobj + BITS_PER_MAPWORD - 1) / BITS_PER_MAPWORD;
    }
    bp->magic = BIBOPMAGIC;
    bp->u.bibop.objsize = size;
    bp->u.bibop.objfree = nobj;
    bp->u.bibop.maxobjfree = nobj;
    bp->u.bibop.freemap = (void *) ((char *)bp + CEIL(sizeof(struct descr)));
    bp->u.bibop.nextfree = 0;
    bp->u.bibop.maxfree = nmap;
    bp->u.bibop.objs = (void *) ((char *)bp->u.bibop.freemap + CEIL(nmap * sizeof(mapword)));
    bp->dnext = 0;

    /* fill the freemap */
    for(i = 0; i < nmap-1; i++)
	bp->u.bibop.freemap[i] = (mapword)~0;
    j = nobj % BITS_PER_MAPWORD; /* no of bits needed in last word, or 0 all are needed */
    if (j)
	bp->u.bibop.freemap[i] = ((mapword)~0) >> (BITS_PER_MAPWORD - j);
    else
	bp->u.bibop.freemap[i] = (mapword)~0;
    return bp;
}

/* External interface.  Should be called before first malloc. */
static void
bibop_init()
{
    int mins, maxs;
    double sfact, smult, s;
    int i;

    if (initdone)
	return;
    initdone++;
    /* set up nextpage to point to space at a page boundary */
    firstsbrk = nextpage = (char *)( ( (PTR)sbrk(PAGE_SIZE+1) & PAGEMASK) + PAGE_SIZE );

    mins = SMALL_SIZE;
    maxs = PAGE_SIZE;
    sfact = (double)maxs / mins;
    smult = exp(log(sfact) / NFREE);
    for(s = smult, i = 0; i < NFREE; i++, s *= smult) {
	freehead[i].h.fwd = &freehead[i].h;
	freehead[i].h.bwd = &freehead[i].h;
	freehead[i].maxsize = (int) (mins * s + 0.5);
	/*(void)fprintf(stderr, "bucket %d holds size <%d\n", i, freehead[i].maxsize);*/
#ifdef FREESTAT
	freehead[i].h.head = &freehead[i];
#endif
    }
}

/* External interface.  Show statistics. */
char *
dump_malloc_data()
{
    static char mbuf[3000];
    long long u_bibop, u_alloc, u_big, n_bibop, n_alloc, n_big, a_bibop, a_alloc, a_big;
    struct descr *p;
    int i, n;

    u_bibop = u_alloc = u_big = n_bibop = n_alloc = n_big = a_bibop = a_alloc = a_big = 0;
    for(i = 0; i < (SMALL_SIZE+ALIGNMENT-1)/ALIGNMENT; i++) {
	for(p = bibops[i]; p; p = p->dnext) {
	    n = p->u.bibop.objfree * p->u.bibop.objsize;
	    n_bibop += n;
	    u_bibop += PAGE_SIZE - n;
	    a_bibop += p->u.bibop.objfree;
	}
    }
    for(p = firstpage; p; p = p->dnext) {
	struct block *q;
	for(q = (struct block *)&p->u.medium.data; q->next; q = q->next) {
	    if (q->busy)
		u_alloc += OBJSIZE(q);
	    else
		n_alloc += OBJSIZE(q), a_alloc++;
	}
    }
    u_big = PAGE_SIZE * p_big;
    (void)sprintf(mbuf, "\
%-17s %13s %13s %13s %13s\n\
%-17s %13lld %13lld %13lld %13lld\n\
%-17s %13lld %13lld %13lld %13lld\n\
%-17s (%lld in free pages)\n\
%-17s %13lld %13lld %13lld %13lld\n\
%-17s %13lld %13lld %13lld %13lld\n\
%-17s %13lld %13lld %13lld %13lld\n\
%-17s (%lld free) = %lld bytes (page=%lld)\n\
\n\
%-17s %13lld %13lld %13lld %13lld\n\
%-17s %13lld %13lld %13lld %13lld\n\
%-17s %13lld %13lld %13lld %13lld\n\
\n\
sbrk requests: %lld %lld (a) \n\
", 
	    "",            "small", "medium", "large", "total",
	    "used memory", u_bibop, u_alloc, u_big, u_bibop+u_alloc+u_big, 
	    "free memory", n_bibop, n_alloc, n_big, n_bibop+n_alloc+n_big+p_free*PAGE_SIZE,
            " ", p_free*PAGE_SIZE, 
	    "used blocks", m_bibop-f_bibop, m_alloc-f_alloc, m_big-f_big, m_bibop+m_alloc+m_big-(f_bibop+f_alloc+f_big),
	    "free blocks", a_bibop, a_alloc, a_big, a_bibop + a_alloc + a_big,
	    "allocated pages", p_bibop, p_alloc, p_big, p_bibop+p_alloc+p_big+p_free+1,
            " ", p_free, (p_bibop+p_alloc+p_big+p_free+1)*PAGE_SIZE, (long long)PAGE_SIZE,

	    "# of malloc()", m_bibop, m_alloc, m_big, m_bibop+m_alloc+m_big,
	    "# of free()", f_bibop, f_alloc, f_big, f_bibop+f_alloc+f_big,
	    "total allocation", s_bibop, s_alloc, s_big, s_bibop+s_alloc+s_big,
	    p_bibop+p_alloc+p_big+p_free+1, (p_bibop+p_alloc+p_big+p_free+1)*PAGE_SIZE);
#ifdef FREESTAT
    {
	char xxxxx[2000];
	int k, tf;
	(void)strcpy(xxxxx, "medium sizes:\n size unus  split   free   join  alloc join1 join2 drop  real why  skip\n");
	for(tf = 0, k = 0; k < NFREE; k++) {
	    struct freeblk *p;
	    int s;
	    for(s = 0, p = freehead[k].h.fwd; p != &freehead[k].h; p = p->fwd, s++)
		;
	    (void)sprintf(xxxxx+strlen(xxxxx), "%5d %4d %6d %6d %6d %6d %5d %5d %4d %5d %3d %5d\n",
		    freehead[k].maxsize, s, 
		    freehead[k].nsplit, freehead[k].nfree, freehead[k].njoin,
		    freehead[k].nalloc, freehead[k].njoin1, freehead[k].njoin2, freehead[k].ndrop, 
		    freehead[k].nreal, freehead[k].nwhy, freehead[k].nskip);
	    tf += s;
	}
	(void)sprintf(xxxxx+strlen(xxxxx), "tot=%d(%d)\n", tf, a_alloc);
	(void)strcat(mbuf, xxxxx);
    }
#endif
    return mbuf;
}
