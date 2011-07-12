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
#include <sys/socket.h>
#include "config.h"
#include "lint.h"
#include "nqueue.h"

/*
 * Network Queues
 */

/*
 * Initialize a network queue.  The read and write pointers are set to the
 * beginning of the queue, and the length is set to 0.
 */
void
nq_init(nqueue_t *nq)
{
    nq->nq_len = 0;
    nq->nq_rptr = 0;
    nq->nq_wptr = 0;
}

/*
 * Allocate a network queue of a given (data) size.
 */
nqueue_t *
nq_alloc(u_short size)
{
    nqueue_t *nq;

    nq = xalloc(sizeof (nqueue_t) + size);
    nq->nq_size = size;
    nq_init(nq);

    return nq;
}

/*
 * Free a network queue.
 */
void
nq_free(nqueue_t *nq)
{
    free(nq);
}

/*
 * Return the (maximum) size of a network queue.
 */
u_short
nq_size(nqueue_t *nq)
{
    return nq->nq_size;
}

/*
 * Return the (current) length of a network queue.
 */
u_short
nq_len(nqueue_t *nq)
{
    return nq->nq_len;
}

/*
 * Return the amount of available space on a network queue.  The available
 * space is defined as the maximum size less the current length.
 */
u_short
nq_avail(nqueue_t *nq)
{
    return nq->nq_size - nq->nq_len;
}

/*
 * Return a pointer to the next character to be read in a network queue.
 */
u_char *
nq_rptr(nqueue_t *nq)
{
    return (u_char *)(nq + 1) + nq->nq_rptr;
}

/*
 * Return a pointer to the next character to be written in a network queue.
 */
u_char *
nq_wptr(nqueue_t *nq)
{
    return (u_char *)(nq + 1) + nq->nq_wptr;
}

/*
 * Determine whether a network queue is empty.  A network queue is empty when
 * its length is 0.
 */
int
nq_empty(nqueue_t *nq)
{
    return nq->nq_len == 0;
}

/*
 * Determine whether a network queue is full.  A network queue is full when
 * its length is equal to its size.
 */
int
nq_full(nqueue_t *nq)
{
    return nq->nq_len == nq->nq_size;
}

/*
 * Get a character from the head of a network queue.  N.B. It is assumed
 * that the caller has determined the queue was not empty prior to calling
 * this function.
 */
u_char
nq_getc(nqueue_t *nq)
{
    u_char c;

    c = *nq_rptr(nq);
    nq->nq_len--, nq->nq_rptr++;
    if (nq->nq_rptr == nq->nq_size)
        nq->nq_rptr = 0;

    return c;
}

/*
 * Return a character to the head of a network queue.  N.B. It is assumed
 * that the caller has determined the queue was not full prior to calling
 * this function.
 */
void
nq_ungetc(nqueue_t *nq, u_char c)
{
    nq->nq_len++;
    if (nq->nq_rptr-- == 0)
        nq->nq_rptr = nq->nq_size - 1;
    *nq_rptr(nq) = c;
}

/*
 * Append a character to the tail of a network queue.  N.B. It is assumed
 * that the caller has determined the queue was not full prior to calling
 * this function.
 */
void
nq_putc(nqueue_t *nq, u_char c)
{
    *nq_wptr(nq) = c;
    nq->nq_len++, nq->nq_wptr++;
    if (nq->nq_wptr == nq->nq_size)
        nq->nq_wptr = 0;
}

/*
 * Append a string to the tail of a network queue.  N.B. It is assumed
 * that the caller has determined the queue has adequate space available
 * prior to calling this function.
 */
void
nq_puts(nqueue_t *nq, u_char *cp)
{
    int len, size;

    len = strlen((char *)cp);

    while (len > 0)
    {
	if (nq->nq_rptr > nq->nq_wptr)
	    size = nq->nq_rptr - nq->nq_wptr;
	else
	    size = nq->nq_size - nq->nq_wptr;

	if (size > len)
	    size = len;

	memcpy(nq_wptr(nq), cp, size);

	nq->nq_len += size, nq->nq_wptr += size;
	if (nq->nq_wptr == nq->nq_size)
	    nq->nq_wptr = 0;

	cp += size, len -= size;
    }
}

/*
 * Remove a character from the tail of a network queue.  N.B. It is assumed
 * that the caller has determined the queue was not empty prior to calling
 * this function.
 */
void
nq_unputc(nqueue_t *nq)
{
    nq->nq_len--;
    if (nq->nq_wptr-- == 0)
        nq->nq_wptr = nq->nq_size - 1;
}

/*
 * Receive data from a socket and append it to a network queue.  N.B. It is
 * assumed that the caller has determined the queue is not full prior to
 * calling this function.
 */
int
nq_recv(nqueue_t *nq, int fd, u_int *uip)
{
    int len, cc;

    if (nq->nq_rptr > nq->nq_wptr)
	len = nq->nq_rptr - nq->nq_wptr;
    else
	len = nq->nq_size - nq->nq_wptr;

    cc = recv(fd, nq_wptr(nq), len, 0);

    if (cc > 0)
    {
	nq->nq_len += cc, nq->nq_wptr += cc;
	if (nq->nq_wptr == nq->nq_size)
	    nq->nq_wptr = 0;

	if (uip != NULL)
	    *uip += cc;
    }

    return cc;
}

/*
 * Send data from a network queue to a socket.
 */
int
nq_send(nqueue_t *nq, int fd, u_int *uip)
{
    int len, cc = 0;

    while (nq->nq_len > 0)
    {
	len = nq->nq_size - nq->nq_rptr;

	if (len > nq->nq_len)
	    len = nq->nq_len;

	cc = send(fd, nq_rptr(nq), len, 0);

	if (cc > 0)
	{
	    nq->nq_len -= cc, nq->nq_rptr += cc;
	    if (nq->nq_rptr == nq->nq_size)
		nq->nq_rptr = 0;

	    if (uip != NULL)
		*uip += cc;
	}

	if (cc != len)
	    break;
    }

    return cc;
}
