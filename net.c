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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "config.h"
#include "simulate.h"
#include "lint.h"
#include "nqueue.h"

#ifndef IPTOS_LOWDELAY
#define	IPTOS_LOWDELAY	0x10
#endif

/*
 * Enable non-blocking I/O on a socket.
 */
void
enable_nbio(int fd)
{
   if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
      fatal("enable_nbio: fcntl(F_SETFL, O_NONBLOCK) errno = %d.\n",
          errno);
}

/*
 * Enable keep-alive on a socket.
 */
void
enable_keepalive(int s)
{
    int keepalive = 1;

    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char *)&keepalive, sizeof (keepalive)) == -1)
	fatal("enable_keepalive: setsockopt() errno = %d.\n", errno);
}

/*
 * Enable address re-use on a socket.
 */
void
enable_reuseaddr(int s)
{
    int reuse = 1;

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof (reuse)) == -1)
	fatal("enable_reuseaddr: setsockopt() errno = %d.\n", errno);
}

/*
 * Enable in-line out-of-band data.
 */
void
enable_oobinline(int s)
{
    int oobinline = 1;

    if (setsockopt(s, SOL_SOCKET, SO_OOBINLINE, (char *)&oobinline, sizeof (oobinline)) == -1)
	fatal("enable_oobinline: setsockopt() errno = %d.\n", errno);
}

/*
 * Enable v6only on ipv6 sockets.
 */
void
enable_v6only(int s)
{
    int on = 1;

    if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&on, sizeof (on)) == -1)
        fatal("enable_v6only: setsockopt() errno = %d.\n", errno);
}


/*
 * Enable Low-Delay IP Type-of-service.
 */
void
enable_lowdelay(int s)
{
#if defined(IPPROTO_IP) && defined(IP_TOS)
    int tos = IPTOS_LOWDELAY;

    setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof (tos));
// This seems to be causing crashes regularly. If this property isn't set,
// then the socket will use its default anyway. In my humble opinion, this
// should not crash the mud so regularly. Outcommented until a proper fix
// can be installed. Mercade.
//    if (setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof (tos)) == -1)
//	fatal("enable_lowdelay: setsockopt() errno = %d.\n", errno);
#endif
}

/*
 * Enable TCP_NODELAY / Disable Nagle
 */
void
enable_nodelay(int s)
{
    int nodelay = 1;
    if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay, sizeof (nodelay)) == -1)
        fatal("enable_nodelay: setsockopt() errno = %d.\n", errno);
}

/*
 * Determine whether we've reached the urgent data pointer.
 */
int
at_mark(int fd)
{
    int atmark = 0;

    if (ioctl(fd, SIOCATMARK, &atmark) != -1)
    {
	if (atmark)
	    return 1;
    }

    return 0;
}

/*
 * Set the kernel receive buffer size for a socket
 */
void
set_rcvsize(int s, int size)
{
#ifdef SO_RCVBUF
    (void)setsockopt(s, SOL_SOCKET, SO_RCVBUF, (void *)&size, sizeof(size));
#endif
}

/*
 * Set the kernel transmit buffer size for a socket
 */
void
set_sndsize(int s, int size)
{
#ifdef SO_SNDBUF
    (void)setsockopt(s, SOL_SOCKET, SO_SNDBUF, (void *)&size, sizeof(size));
#endif
}
