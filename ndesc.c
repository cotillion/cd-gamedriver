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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
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
static ndesc_t **nd_ndesc;

static struct pollfd *pollfds;
static int pollfd_max = 0;
static size_t ndesc_size = 256;
static size_t pollfd_size = 256;

/*
 * Initialize the state associated with a given file descriptor.  Clear the
 * read, write and exception bits in the fd_sets and initialize the associated
 * network descriptor object pointer to NULL.
 */
static void
nd_initfd(int fd)
{
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

    pollfds = malloc(sizeof(struct pollfd) * pollfd_size);
    nd_ndesc = malloc(sizeof(ndesc_t *) * ndesc_size);
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
nd_attach(int fd, void *rfunc, void *wfunc, void *xfunc, void *sfunc, void *vp)
{
    ndesc_t *nd;

    nd = xalloc(sizeof (ndesc_t));
    nd->nd_fd = fd;
    nd->nd_mask = 0;
    nd->nd_rfunc = rfunc;
    nd->nd_wfunc = wfunc;
    nd->nd_xfunc = xfunc;
    nd->nd_sfunc = sfunc;
    nd->nd_vp = vp;
    nd->nd_poll = -1;
    nd_append(nd);

    if (fd >= ndesc_size) {
        ndesc_size *= 2;
        nd_ndesc = realloc(nd_ndesc, ndesc_size * sizeof(ndesc_t *));
    }
    nd_ndesc[fd] = nd;

    nd->nd_poll = pollfd_max;
    if (nd->nd_poll >= pollfd_size) {
        pollfd_size *= 2;
        pollfds = realloc(pollfds, pollfd_size * sizeof(struct pollfd));
    }

    pollfd_max++;
    pollfds[nd->nd_poll].fd = fd;
    pollfds[nd->nd_poll].events = 0;
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

    pollfd_max--;
    if (nd->nd_poll < pollfd_max) {
        ndesc_t *tail = nd_ndesc[pollfds[pollfd_max].fd];
        pollfds[nd->nd_poll] = pollfds[pollfd_max];
        tail->nd_poll = nd->nd_poll;
    }

    free(nd);
}

/*
 * Enable read, write, exception and/or clean-up callbacks for a
 * network descriptor.
 */
void
nd_enable(ndesc_t *nd, int mask)
{
    nd->nd_mask |= mask & ND_MASK;

    if (mask & ND_R)
        pollfds[nd->nd_poll].events |= POLLIN;
    if (mask & ND_W)
        pollfds[nd->nd_poll].events |= POLLOUT;
    if (mask & ND_X)
        pollfds[nd->nd_poll].events |= POLLPRI;
}

/*
 * Disable read, write, exception and/or clean-up callbacks for a
 * network descriptor.
 */
void
nd_disable(ndesc_t *nd, int mask)
{
    nd->nd_mask &= ~mask;

    if (mask & ND_R)
        pollfds[nd->nd_poll].events &= ~POLLIN;
    if (mask & ND_W)
        pollfds[nd->nd_poll].events &= ~POLLOUT;
    if (mask & ND_X)
        pollfds[nd->nd_poll].events &= ~POLLPRI;
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
    int nfds;
    ndesc_t *nd;
    struct timeval tv = *tvp;
    struct timespec ts;

    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000;

    /* Nothing else to process so wait on I/O */
    nfds = ppoll(pollfds, pollfd_max, &ts, NULL);

    if (nfds == -1) /* Error!!! */
        return;

    for (int pfd = 0; pfd < pollfd_max; pfd++) {
        nd = nd_ndesc[pollfds[pfd].fd];

        if (pollfds[pfd].revents & POLLPRI) {
            (*nd->nd_xfunc)(nd, nd->nd_vp);
            pollfds[pfd].revents = 0;
        }

        if (pollfds[pfd].revents & POLLOUT) {
            (*nd->nd_wfunc)(nd, nd->nd_vp);
            pollfds[pfd].revents = 0;
        }

        if (pollfds[pfd].revents & POLLIN) {
            (*nd->nd_rfunc)(nd, nd->nd_vp);
            pollfds[pfd].revents = 0;
        }
    }
}

/*
 * Invoke the shutdown method for all network descriptors.
 */
void
nd_shutdown(void)
{
    struct timeval tv;
    ndesc_t *nd, *next;

    tv.tv_sec = tv.tv_usec = 0;
    nd_select(&tv);

    for (nd = nd_head; nd != NULL; nd = next)
    {
        next = nd->nd_next;

        if (nd->nd_sfunc != NULL)
            (*nd->nd_sfunc)(nd, nd->nd_vp);
    }
}
