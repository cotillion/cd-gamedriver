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
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "config.h"
#include "lint.h"
#include "net.h"
#include "nqueue.h"
#include "ndesc.h"
#include "hname.h"
#include "backend.h"

#ifndef INADDR_LOOPBACK
#define	INADDR_LOOPBACK		0x7f000001
#endif

#ifndef INADDR_NONE
#define	INADDR_NONE		0xffffffff
#endif

#ifndef EPROTO
#define EPROTO EPROTOTYPE
#endif

#ifndef NO_IP_DEMON

/*
 * Hostname Server
 */

/*
 * Hostname Server Control Block.
 */

typedef struct {
    u_char	h_flags;
    nqueue_t *	h_rawq;
    nqueue_t *	h_canq;
    nqueue_t *	h_outq;
    ndesc_t  *  h_nd;
    struct task *task;
    hname_callback_t callback;
    void (*shutdown_callback)(void *);
} hname_t;


/*
 * Hostname Server Flags.
 */
#define	HF_CLOSE	0x01
#define	HF_ENABW	0x04
#define HF_INPUT        0x08
/*
 * Queue Sizes.
 */
#define	HNAME_RAWQ_SIZE	128
#define	HNAME_CANQ_SIZE	128
#define	HNAME_OUTQ_SIZE	128


/*
 * Allocate a Hostname Server control block.
 */
static hname_t *
hname_alloc(void)
{
    hname_t *hp;

    hp = xalloc(sizeof (hname_t));
    hp->h_flags = 0;
    hp->h_rawq = nq_alloc(HNAME_RAWQ_SIZE);
    hp->h_canq = nq_alloc(HNAME_CANQ_SIZE);
    hp->h_outq = nq_alloc(HNAME_OUTQ_SIZE);
    hp->task = 0;
    return hp;
}

/*
 * Free a Hostname Server control block.
 */
static void
hname_free(hname_t *hp)
{
    nq_free(hp->h_rawq);
    nq_free(hp->h_canq);
    nq_free(hp->h_outq);
    remove_task(hp->task);
    free(hp);
}

/*
 * Process a response from the hname server.
 */
static void
hname_input(hname_t *hp)
{
    int ntok, lport, rport;
    char *tok[5] = {};
    char *addr, *rname, *ip_name, *cp, *end;

    if ((hp->h_flags & HF_INPUT) == 0)
	return;
    
    hp->h_flags &= ~HF_INPUT;
    cp = (char *)nq_rptr(hp->h_canq);

    for (ntok = 0; ntok < 5; ntok++)
    {
        tok[ntok] = (char *)cp;

        end = strchr(cp, ',');

        if (end != NULL)
        {
            *end = '\0';
            end++;
            cp = end;
        }
    }
    
    addr = tok[0];
    lport = (tok[1] == NULL ? 0 : atoi(tok[1]));
    rport = (tok[2] == NULL ? 0 : atoi(tok[2]));
    ip_name = tok[3];
    rname = tok[4];
    hp->callback(addr, lport, rport, ip_name, rname);
    
    nq_init(hp->h_canq);
}


/*
 * Read the network into the raw input queue.
 */
static void
hname_readbytes(hname_t *hp)
{
    int cc;
    u_char c;

    if (!nq_full(hp->h_rawq))
    {
	cc = nq_recv(hp->h_rawq, nd_fd(hp->h_nd), NULL);
	if (cc == -1)
	{
	    switch (errno)
	    {
	    case EWOULDBLOCK:
	    case EINTR:
	    case EPROTO:
	      break;

	    default:
	        hp->h_flags |= HF_CLOSE;
	    }
	    return;
	}
	if (cc == 0)
	{
	    hp->h_flags |= HF_CLOSE;
	    return;
	}
    }
    for (;;)
    {
	if (nq_empty(hp->h_rawq))
	{
	    nq_init(hp->h_rawq);
	    return;
	}
	c = nq_getc(hp->h_rawq);
	if (c == '\n') {
	    break;
	}
	
	if (!nq_full(hp->h_canq))
	    nq_putc(hp->h_canq, c);
    }
    
    if (nq_full(hp->h_canq))
	nq_unputc(hp->h_canq);
    nq_putc(hp->h_canq, '\0');
    hp->h_flags |= HF_INPUT;
}


static void
hname_shutdown(hname_t *hp)
{
    
    (void)close(nd_fd(hp->h_nd));
    nd_detach(hp->h_nd);
    hp->h_nd = 0;
    if (hp->shutdown_callback)
	hp->shutdown_callback(hp);
    hname_free(hp);
}


static void
hname_readline(void *vp)
{
    hname_t *hp = vp;
    
    if (hp->h_flags & HF_CLOSE)
    {
	hname_shutdown(hp);
	return;
    }
    hname_input(hp);
    hname_readbytes(hp);
    if (hp->h_flags & HF_CLOSE)
    {
	hname_shutdown(hp);
	return;
    }
    if (hp->h_flags & HF_INPUT) {
	reschedule_task(hp->task);
	nd_disable(hp->h_nd, ND_R);
	return;
    }
    hp->task = NULL;
    nd_enable(hp->h_nd, ND_R);
}

/*
 * Process a network disconnect indication.
 */
static void
hname_disconnect(ndesc_t *nd, hname_t *hp)
{
    hp->h_flags |= HF_CLOSE;
    if (!hp->task)
	hp->task = create_task(hname_readline, hp);
}

/*
 * Write the contents of the output queue to the network.
 */
static void
hname_write(ndesc_t *nd, hname_t *hp)
{
    if (!nq_empty(hp->h_outq))
    {
	if (nq_send(hp->h_outq, nd_fd(nd), NULL) == -1)
	{
	    switch (errno)
	    {
	    case EWOULDBLOCK:
	    case EINTR:
	    case EPROTO:
		break;

	    default:
		hname_disconnect(nd, hp);
		return;
	    }
	}

	if (!nq_empty(hp->h_outq))
	    return;
    }

    nq_init(hp->h_outq);

    nd_disable(nd, ND_W);
}

/*
 * Close the Hostname Service connection and free the associated resources.
 */

static void
hname_read(ndesc_t *nd, hname_t *hp)
{
    hname_readbytes(hp);
    if (hp->h_flags & (HF_INPUT | HF_CLOSE)) {
	if (!hp->task)
	    hp->task = create_task(hname_readline, hp);
	nd_disable(nd, ND_R);
    }
}

/*
 * Initialize the Hostname Server.
 */
void *
hname_init(hname_callback_t callback, void (*shutdown_callback)(void *))
{
    int pid;
    char path[MAXPATHLEN];
    hname_t *hp;
    int sockets[2];

    if(socketpair(AF_LOCAL, SOCK_STREAM, 0, sockets) == -1)
	return NULL;

    pid = fork();
    if (pid == -1)
    {
	(void)close(sockets[0]);
	(void)close(sockets[1]);
	return NULL;
    }

    if (pid == 0)
    {
	(void)close(sockets[0]);

	(void)dup2(sockets[1], 0);
	(void)dup2(sockets[1], 1);

	for (int i = 3; i < FD_SETSIZE; i++)
	    (void)close(i);

	strncpy(path, BINDIR, sizeof (path));
	strncat(path, "/hname", sizeof (path));

	(void)execl(path, "hname", NULL);

        (void)fprintf(stderr, "exec of hname failed.\n");

	exit(1);
    }
    
    enable_nbio(sockets[0]);
    
    hp = hname_alloc();
    hp->callback = callback;
    hp->shutdown_callback = shutdown_callback;
    hp->h_nd = nd_attach(sockets[0], hname_read, hname_write, NULL,
			 NULL, hname_disconnect, hp);
    nd_enable(hp->h_nd, ND_R);
    return hp;
}

/*
 * Send a request to the hname server.
 */
void
hname_sendreq(void *vp, const char *addr, u_short lport, u_short rport)
{
    char req[128];
    hname_t *hp = vp;
    
    if (hp == NULL || hp->h_nd == NULL)
	return;

    snprintf(req, sizeof(req), "%s,%hu,%hu\n", addr, lport, rport);

    if (nq_avail(hp->h_outq) < strlen(req))
	return;

    nq_puts(hp->h_outq, (u_char *)req);
    nd_enable(hp->h_nd, ND_W);
}

#endif
