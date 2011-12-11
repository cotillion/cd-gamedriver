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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"
#include "lint.h"
#include "main.h"
#include "interpret.h"
#include "simulate.h"
#include "ndesc.h"
#include "nqueue.h"
#include "net.h"
#include "backend.h"
#include "tcpsvc.h"

#ifndef EPROTO
#define	EPROTO	EPROTOTYPE
#endif

#ifdef SERVICE_PORT

/*
 * TCP Service
 */

/*
 * TCP Service Control Block.
 */
typedef struct {
    u_char	ts_flags;
    nqueue_t *	ts_canq;
    nqueue_t *	ts_rawq;
    ndesc_t  *  ts_nd;
    struct task *task;
} tcpsvc_t;

/*
 * TCP Service Flags.
 */
#define	TF_CLOSE	0x01

/*
 * Queue Sizes.
 */
#define	TCPSVC_RAWQ_SIZE        32768	
#define	TCPSVC_CANQ_SIZE        32768	

/*
 * Maximum # of concurrent TCP Service.
 */
#define	TCPSVC_MAX		32

// static ndesc_t *tcpsvc_nd = NULL;
static int tcpsvc_count = 0;

/*
 * Allocate a TCP Service control block.
 */
static tcpsvc_t *
tcpsvc_alloc(void)
{
    tcpsvc_t *tsp;

    tsp = xalloc(sizeof (tcpsvc_t));
    tsp->ts_flags = 0;
    tsp->ts_rawq = nq_alloc(TCPSVC_RAWQ_SIZE);
    tsp->ts_canq = nq_alloc(TCPSVC_CANQ_SIZE);
    tsp->ts_nd = 0;
    tsp->task = 0;
    return tsp;
}

/*
 * Free a TCP Service control block.
 */
static void
tcpsvc_free(tcpsvc_t *tsp)
{
    if (tsp->ts_rawq)
        nq_free(tsp->ts_rawq);
    tsp->ts_rawq = NULL;
    if (tsp->ts_canq)
        nq_free(tsp->ts_canq);
    tsp->ts_canq = NULL;
    remove_task(tsp->task);
    tsp->task = NULL;
    free(tsp);
}

/*
 * Process a disconnect indication.
 */
static void
tcpsvc_disconnect(ndesc_t *nd, tcpsvc_t *tsp)
{
    tsp->ts_flags |= TF_CLOSE;
    nd_enable(tsp->ts_nd, ND_W);
    nd_disable(tsp->ts_nd, ND_R);
}

/*
 * Close a TCP Service connection and free the associated resources.
 */
static void
tcpsvc_shutdown(ndesc_t *nd, tcpsvc_t *tsp)
{
    if (tsp)
	tsp->ts_flags |= TF_CLOSE;
}

static void
tcpsvc_process(void *vp)
{
    struct svalue *svp;
    struct gdexception exception_frame;
    tcpsvc_t *tsp = vp;

    nd_enable(tsp->ts_nd, ND_W);
    tsp->task = 0;

    if (tsp->ts_flags & TF_CLOSE)
    {
        close(nd_fd(tsp->ts_nd));
        nd_detach(tsp->ts_nd);
        tcpsvc_free(tsp);
        tcpsvc_count--;
	return;
    }

    update_tcp_av();

    if (nq_full(tsp->ts_canq))
    {
	nq_init(tsp->ts_canq);
	nq_puts(tsp->ts_canq, (u_char *)"ERROR Service request too long.\n");
	tcpsvc_disconnect(tsp->ts_nd, tsp);
	return;
    }


    exception_frame.e_exception = exception;
    exception_frame.e_catch = 0;

    exception = &exception_frame;

    if (setjmp(exception_frame.e_context) == 0)
    {
	push_string((char *)nq_rptr(tsp->ts_canq), STRING_MSTRING);
	svp = apply_master_ob(M_INCOMING_SERVICE, 1);
    }
    else
    {
	svp = NULL;
    }

    exception = exception->e_exception;

    nq_init(tsp->ts_canq);

    if (svp == NULL || svp->type != T_STRING)
    {
	nq_puts(tsp->ts_canq, (u_char *)"ERROR Service calls not supported.\n");
	tcpsvc_disconnect(tsp->ts_nd, tsp);
	return;
    }

    if (strlen(svp->u.string) > nq_size(tsp->ts_canq))
    {
	nq_puts(tsp->ts_canq, (u_char *)"ERROR Service response too long.\n");
	tcpsvc_disconnect(tsp->ts_nd, tsp);
	return;
    }

    nq_puts(tsp->ts_canq, (u_char *)svp->u.string);
}

/*
 * Read the network into the raw input queue.
 */
static void
tcpsvc_read(ndesc_t *nd, tcpsvc_t *tsp)
{
    int cc;
    char c;

    if (!nq_full(tsp->ts_rawq))
    {
	cc = nq_recv(tsp->ts_rawq, nd_fd(nd), NULL);
	if (cc == -1)
	{
	    switch (errno)
	    {
	    case EWOULDBLOCK:
	    case EINTR:
	    case EPROTO:
		break;

	    default:
		tcpsvc_disconnect(nd, tsp);
                nd_disable(nd, ND_W);
		tsp->task = create_task(tcpsvc_process, tsp);
		return;
	    }
	}

	if (cc == 0)
	{
	    tcpsvc_disconnect(nd, tsp);
            nd_disable(nd, ND_W);
	    tsp->task = create_task(tcpsvc_process, tsp);
	    return;
	}

    }
    for (;;)
    {
	if (nq_empty(tsp->ts_rawq))
	{
	    nq_init(tsp->ts_rawq);
	    return;
	}
	c = nq_getc(tsp->ts_rawq);
	if (c == '\n')
	    break;
	if (!nq_full(tsp->ts_canq))
	    nq_putc(tsp->ts_canq, c);
    }
    nd_disable(tsp->ts_nd, ND_R);
    if (!nq_full(tsp->ts_canq))
	nq_putc(tsp->ts_canq, '\0');
    tsp->task = create_task(tcpsvc_process, tsp);
}

/*
 * Write the contents of the canonical queue to the network.
 */
static void
tcpsvc_write(ndesc_t *nd, tcpsvc_t *tsp)
{
    if (!nq_empty(tsp->ts_canq))
    {
	if (nq_send(tsp->ts_canq, nd_fd(nd), NULL) == -1)
	{
	    switch (errno)
	    {
	    case EWOULDBLOCK:
	    case EINTR:
	    case EPROTO:
		break;

	    default:
		tcpsvc_disconnect(nd, tsp);
                nd_disable(nd, ND_W);
	        tsp->task = create_task(tcpsvc_process, tsp);
		return;
	    }
	}

	if (!nq_empty(tsp->ts_canq))
	    return;
    }

    nq_init(tsp->ts_canq);
    nd_disable(nd, ND_W);
    /* If set to close close soon/now */
    if (tsp->ts_flags & TF_CLOSE)
    {

	tsp->task = create_task(tcpsvc_process, tsp);
	return;
    }
    nd_enable(nd, ND_R);
}

/*
 * Accept a TCP Service connection.
 */
static void
tcpsvc_accept(void *vp)
{
    int s;
    char host[NI_MAXHOST], port[NI_MAXSERV];
    struct sockaddr_storage addr;
    socklen_t addrlen;
    tcpsvc_t *tsp;
    ndesc_t *nd = vp;
    struct svalue *svp;
    struct gdexception exception_frame;


    nd_enable(nd, ND_R);

    addrlen = sizeof (addr);
    s = accept(nd_fd(nd), (struct sockaddr *)&addr, &addrlen);
    if (s == -1)
    {
	switch (errno)
	{
	  default:
	    fatal("svc_server: accept() errno = %d.\n", errno);
	  case EWOULDBLOCK:
	  case EINTR:
	  case EPROTO:
	    return;
	}
    }

    getnameinfo((struct sockaddr *)&addr, addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    
    exception_frame.e_exception = exception;
    exception_frame.e_catch = 0;
    exception = &exception_frame;

    if (setjmp(exception_frame.e_context) == 0)
    {
        push_string(host, STRING_MSTRING);
        push_number(atoi(port));
        svp = apply_master_ob(M_VALID_INCOMING_SERVICE, 2);
    }
    else
    {
        svp = NULL;
    }

    exception = exception->e_exception;

    if (svp == NULL || svp->type != T_NUMBER || svp->u.number == 0)
    {
        fprintf(stderr, "SERVICE PORT ACCESS DENIED FROM [%s]:%s\n", host, port);
        close(s);
        return;
    }

    enable_nbio(s);

    tsp = tcpsvc_alloc();
    tsp->ts_nd = nd_attach(s, tcpsvc_read, tcpsvc_write, NULL, NULL,
			   tcpsvc_shutdown, tsp);

    if (++tcpsvc_count > TCPSVC_MAX)
    {
	nq_puts(tsp->ts_canq, (u_char *)"ERROR Too many services in use.\n");
	nd_enable(tsp->ts_nd, ND_W);
	tcpsvc_disconnect(tsp->ts_nd, tsp);
	return;
    }
    nd_enable(tsp->ts_nd, ND_R);
}

static void
tcpsvc_ready(ndesc_t *nd, void *vp)
{
    nd_disable(nd, ND_R);
    create_task(tcpsvc_accept, nd);
}

/*
 * Initialize the TCP Service Manager.
 */
void
tcpsvc_init(u_short port_nr)
{
    int s, e;
    struct addrinfo hints;
    struct addrinfo *res, *rp;
    char host[NI_MAXHOST], port[NI_MAXSERV];
    ndesc_t *nd;

    if (service_port < 0)
	return;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port, sizeof(port), "%d", port_nr);
    e = getaddrinfo(NULL, port, &hints, &res);

    if (e)
    {
        perror(gai_strerror(e));
        exit(1);
    }

    s = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (s == -1)
            continue;

        enable_reuseaddr(s);
       
        if (rp->ai_family == AF_INET6)
            enable_v6only(s);

        getnameinfo(rp->ai_addr, rp->ai_addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);

        if (bind(s, rp->ai_addr, rp->ai_addrlen) == 0)
        {
            /* Success */
            printf("Listening to tcp service port: %s:%s\n",  host, port);


            enable_reuseaddr(s);
            enable_nbio(s);

            if (listen(s, 5) == -1)
            {
                close(s);
                return;
            }

            nd = nd_attach(s, tcpsvc_ready, NULL, NULL, NULL, tcpsvc_shutdown, NULL);
            nd_enable(nd, ND_R);

        } else {
            fatal("Failed to bind tcp service port: %s:%s\n", host, port);
            close(s);
        }
    }
}

#endif
