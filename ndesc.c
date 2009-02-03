/*-
 * Copyright (c) 1997 Dave Richards <dave@synergy.org>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include "config.h"
#include "lint.h"
#include "ndesc.h"

/*
 * Network Descriptor Manager
 */

static ndesc_t *nd_head;
static ndesc_t *nd_tail;
static int nd_max_fd;
static fd_set nd_rfds;
static fd_set nd_wfds;
static fd_set nd_xfds;
static ndesc_t *nd_ndesc[FD_SETSIZE];
static int nd_do_gc;

/*
 * Initialize the state associated with a given file descriptor.  Clear the
 * read, write and exception bits in the fd_sets and initialize the associated
 * network descriptor object pointer to NULL.
 */
static void
nd_initfd(int fd)
{
    FD_CLR(fd, &nd_rfds);
    FD_CLR(fd, &nd_wfds);
    FD_CLR(fd, &nd_xfds);

    nd_ndesc[fd] = NULL;
}

/*
 * Initialize the network descriptor module.
 */
void
nd_init(void)
{
    nd_head = NULL;
    nd_tail = NULL;

    nd_max_fd = -1;

    FD_ZERO(&nd_rfds);
    FD_ZERO(&nd_wfds);
    FD_ZERO(&nd_xfds);

    memset(nd_ndesc, 0, sizeof (nd_ndesc));

    nd_do_gc = 0;
}

/*
 * Append a network descriptor object to the list of attached network
 * descriptors.
 */
static void
nd_append(ndesc_t *nd)
{
    nd->nd_next = NULL;
    if ((nd->nd_prev = nd_tail) != NULL)
	nd->nd_prev->nd_next = nd;
    else
	nd_head = nd;
    nd_tail = nd;
}

/* Make a network descriptor the tail of the list */
static void
nd_set_tail(ndesc_t *nd)
{
    if (nd == nd_tail)
        return;

    /* Make the list circular */
    nd_head->nd_prev = nd_tail;
    nd_tail->nd_next = nd_head;
    /* Move the head/tail */
    nd_head = nd->nd_next;
    nd_tail = nd;
    /* Cut the circle */
    nd_tail->nd_next = NULL;
    nd_head->nd_prev = NULL;
}
/*
 * Remove a network descriptor object from the list of attached network
 * descriptors.
 */
static void
nd_remove(ndesc_t *nd)
{
    if (nd->nd_next != NULL)
	nd->nd_next->nd_prev = nd->nd_prev;
    else
	nd_tail = nd->nd_prev;

    if (nd->nd_prev != NULL)
	nd->nd_prev->nd_next = nd->nd_next;
    else
	nd_head = nd->nd_next;
}

/*
 * Return the file descriptor currently associated with a network descriptor
 * object.  N.B. This can change over time!
 */
int
nd_fd(ndesc_t *nd)
{
    return nd->nd_fd;
}

/*
 * Return the user context pointer associated with a network descriptor
 * object.
 */
void *
nd_vp(ndesc_t *nd)
{
    return nd->nd_vp;
}

/*
 * Attach a file descriptor to the network descriptor manager.
 */
ndesc_t *
nd_attach(int fd, void *rfunc, void *wfunc, void *xfunc, void *cfunc,
    void *sfunc, void *vp)
{
    ndesc_t *nd;

    nd = xalloc(sizeof (ndesc_t));
    nd->nd_fd = fd;
    nd->nd_mask = 0;
    nd->nd_rfunc = rfunc;
    nd->nd_wfunc = wfunc;
    nd->nd_xfunc = xfunc;
    nd->nd_cfunc = cfunc;
    nd->nd_sfunc = sfunc;
    nd->nd_vp = vp;

    nd_append(nd);

    if (nd_max_fd < fd)
        nd_max_fd = fd;

    nd_ndesc[fd] = nd;

    return nd;
}

/*
 * Detach a file descriptor from the network descriptor manager.
 */
void
nd_detach(ndesc_t *nd)
{
    nd_remove(nd);

    nd_initfd(nd->nd_fd);

    free(nd);

    nd_do_gc++;
}

/*
 * Garbage collect file descriptors.  As file descriptors are detached
 * the network descriptor manager attempts to dup() file descriptors to
 * smaller numbers.  This reduces the number of file descriptors select()
 * has to process.
 */
static void
nd_gc(void)
{
    int fd;

    nd_do_gc = 0;
    
    if (nd_max_fd == -1)
        return;

    for (;;)
    {
        while (nd_ndesc[nd_max_fd] == NULL)
            if (--nd_max_fd == -1)
                return;

        fd = dup(nd_max_fd);
        if (fd == -1)
            return;

        if (fd < nd_max_fd)
        {
            if (FD_ISSET(nd_max_fd, &nd_rfds))
                FD_SET(fd, &nd_rfds);
            if (FD_ISSET(nd_max_fd, &nd_wfds))
                FD_SET(fd, &nd_wfds);
            if (FD_ISSET(nd_max_fd, &nd_xfds))
                FD_SET(fd, &nd_xfds);

            nd_ndesc[fd] = nd_ndesc[nd_max_fd];
            nd_ndesc[fd]->nd_fd = fd;

            nd_initfd(nd_max_fd);

            close(nd_max_fd);
        }
        else
        {
            close(fd);
            return;
        }
    }
}

/*
 * Enable read, write, exception and/or clean-up callbacks for a
 * network descriptor.
 */
void
nd_enable(ndesc_t *nd, int mask)
{
    int fd;

    nd->nd_mask |= mask & ND_MASK;

    fd = nd->nd_fd;

    if (mask & ND_R)
        FD_SET(fd, &nd_rfds);
    if (mask & ND_W)
        FD_SET(fd, &nd_wfds);
    if (mask & ND_X)
        FD_SET(fd, &nd_xfds);
}

/*
 * Disable read, write, exception and/or clean-up callbacks for a
 * network descriptor.
 */
void
nd_disable(ndesc_t *nd, int mask)
{
    int fd;

    nd->nd_mask &= ~mask;

    fd = nd->nd_fd;

    if (mask & ND_R)
        FD_CLR(fd, &nd_rfds);
    if (mask & ND_W)
        FD_CLR(fd, &nd_wfds);
    if (mask & ND_X)
        FD_CLR(fd, &nd_xfds);
}

/* Call cleanup on one network descriptor and return true.
 * If no descriptor needs cleanup return 0.
 */
static int
nd_cleanup(void)
{
    ndesc_t *nd;
    for (nd = nd_head; nd != NULL; nd = nd->nd_next)
    {
	if (nd->nd_mask & ND_C)
	{
            nd_set_tail(nd);
	    (*nd->nd_cfunc)(nd, nd->nd_vp);
	    if (nd_do_gc)
		nd_gc();
	    return 1;
	}
    }
    return 0;
}
/*
 * Perform a select() on all network descriptors and call the read, write
 * and exception callback functions for those descriptors which select()
 * true.  The callbacks are invoked in the following order: exception
 * callback, write callback and read callback.  If a clean-up callback
 * has been enabled, call it last.
 */
void
nd_select(struct timeval *tvp)
{
    int nfds, fd;
    fd_set rfds, wfds, xfds;
    ndesc_t *nd;
    struct timeval tv = *tvp;
    rfds = nd_rfds;
    wfds = nd_wfds;
    xfds = nd_xfds;

    for (nd = nd_head; nd != NULL; nd = nd->nd_next)
        if (nd->nd_mask & ND_C) {
            tv.tv_sec = tv.tv_usec = 0;
            break;
        }

    /* Nothing else to process so wait on I/O */
    nfds = select(nd_max_fd + 1, &rfds, &wfds, &xfds, &tv);

    if (nfds == -1) /* Error!!! */
        return;

    for (nd = nd_head; nd != NULL; nd = nd->nd_next)
    {
        fd = nd->nd_fd;

        if (FD_ISSET(fd, &xfds))
	    (*nd->nd_xfunc)(nd, nd->nd_vp);

        if (FD_ISSET(fd, &wfds))
	    (*nd->nd_wfunc)(nd, nd->nd_vp);

        if (FD_ISSET(fd, &rfds))
	    (*nd->nd_rfunc)(nd, nd->nd_vp);
    }

    for (nd = nd_head; nd != NULL; nd = nd->nd_next)
    {
        if (nd->nd_mask & ND_C)
        {
            nd_set_tail(nd);
	    (*nd->nd_cfunc)(nd, nd->nd_vp);
	    break;
        }
    }

    if (nd_do_gc)
        nd_gc();
}

/*
 * Invoke the shutdown method for all network descriptors.
 */
void
nd_shutdown(void)
{
    struct timeval tv;
    ndesc_t *nd, *next;

    while(nd_cleanup())
        ;
    tv.tv_sec = tv.tv_usec = 0;
    nd_select(&tv);
    while(nd_cleanup())
        ;
    for (nd = nd_head; nd != NULL; nd = next)
    {
        next = nd->nd_next;
        
	if (nd->nd_sfunc != NULL)
	    (*nd->nd_sfunc)(nd, nd->nd_vp);
    }
    while(nd_cleanup())
        ;
}
