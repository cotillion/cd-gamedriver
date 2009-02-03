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
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
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

#ifdef CATCH_UDP_PORT

#ifndef INADDR_NONE
#define INADDR_NONE	0xffffffff
#endif

/*
 * UDP Service
 */

/*
 * Maximum UDP Datagram Size.
 */
#define	UDPSVC_RAWQ_SIZE	1024

/*
 * Send a UDP datagram.
 */
int
udpsvc_send(udpsvc_t *svc, char *dest, int port, char *cp)
{
    struct sockaddr_in addr;
    int cc;

    if (udpsvc_nd == NULL || port < 0)
	return 0;

    memset(&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(dest);

    if (addr.sin_addr.s_addr == INADDR_NONE)
    {
#ifdef UDP_SEND_HOSTNAME
	struct hostent *hp;

	hp = gethostbyname(addr);
	if (hp == NULL)
	    return 0;
	memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
#else
	return 0;
#endif
    }

    cc = sendto(nd_fd(svc->nd), cp, strlen(cp), 0,
	     (struct sockaddr *)&addr, sizeof (addr));

    return cc != -1;
}

static int
read_datagram(udpsvc_t *svc)
{
    int addrlen, cc;
    struct sockaddr_in addr;

    /* Get another datagram */
    cc = recvfrom(nd_fd(svc->nd), nq_wptr(svc->nq), nq_size(svc->nq) - 1, 0,
	     (struct sockaddr *)&addr, &addrlen);

    if (cc == -1) {
	return 0;
    }

    nq_wptr(svc->nq)[cc] = '\0';

    return 1;
}

static void
udpsvc_process(udpsvc_t *svc)
{
    int addrlen, cc;
    struct sockaddr_in addr;
    struct gdexception exception_frame;
    
    update_udp_av();

    exception_frame.e_exception = exception;
    exception_frame.e_catch = 0;

    exception = &exception_frame;

    
    if (setjmp(exception_frame.e_context) == 0)
    {
	push_string(inet_ntoa(addr.sin_addr), STRING_MSTRING);
	push_string((char *)nq_rptr(svc->nq), STRING_MSTRING);
	(void)apply_master_ob(M_INCOMING_UDP, 2);
    }
    exception = exception->e_exception;
    addrlen = sizeof (addr);

    if (!read_datagram(svc)) {
	nd_enable(svc->nd, ND_R);
	svc->task = 0;
	return;
    }
    reschedule_task(svc->task);
}

/*
 * Read an UPD datagram 
 */
static void
udpsvc_read(ndesc_t *nd, udpsvc_t *svc)
{
    int addrlen, cc;
    struct sockaddr_in addr;
    struct gdexception exception_frame;

    if (read_datagram(svc)) {
	nd_disable(udpsvc_nd, ND_R);
	svc->task = create_task(udpsvc_process);
    }
}

/*
 * Close the UDP Manager session and free the associated resources.
 */
static void
udpsvc_shutdown(ndesc_t *nd, udpsvc_t *svc)
{
    (void)close(nd_fd(nd));
    nq_free(svc->nq);
    nd_detach(nd);
    free(svc);
}

/*
 * Initialize the UDP Manager.
 */
udpsvc_t *
udpsvc_init(int port)
{
    int s;
    struct sockaddr_in addr;
    struct udpsvc_t *svc;
    
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == -1)
	fatal("udp_init: socket() error = %d.\n", errno);

    enable_reuseaddr(s);

    memset(&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (struct sockaddr *)&addr, sizeof (addr)) == -1)
    {
	if (errno == EADDRINUSE) 
	{
	    (void)fprintf(stderr, "UDP Socket already bound!\n");
	    debug_message("UDP Socket already bound!\n");
	    (void)close(s);
	    return 0;
	} 
	else 
	{
	    fatal("udp_init: bind() error = %d.\n", errno);
	}
    }

    enable_nbio(s);
    svc = xalloc(sizeof(*udpsvc));
    svc->nq = nq_alloc(UDPSVC_RAWQ_SIZE);
    svc->nd = nd_attach(s, udpsvc_read, NULL, NULL, NULL, udpsvc_shutdown,
			svc);
    nd_enable(svc->nd, ND_R);
    return svc;
}

#endif /* CATCH_UDP_PORT */
