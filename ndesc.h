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
#ifndef _NDESC_H
#define _NDESC_H
typedef struct ndesc {
    int                 nd_fd;
    int			nd_mask;
    void                (*nd_rfunc)(struct ndesc *, void *);
    void                (*nd_wfunc)(struct ndesc *, void *);
    void                (*nd_xfunc)(struct ndesc *, void *);
    void                (*nd_cfunc)(struct ndesc *, void *);
    void                (*nd_sfunc)(struct ndesc *, void *);
    void *              nd_vp;
    struct ndesc *      nd_next;
    struct ndesc *      nd_prev;
} ndesc_t;

#define	ND_R	0x01
#define	ND_W	0x02
#define	ND_X	0x04
#define	ND_C	0x08
#define	ND_MASK	0x0f

struct timeval;

void nd_init(void);
int nd_fd(ndesc_t *);
void *nd_vp(ndesc_t *);
ndesc_t *nd_attach(int, void *, void *, void *, void *, void *, void *);
void nd_detach(ndesc_t *);
void nd_enable(ndesc_t *, int);
void nd_disable(ndesc_t *, int);
void nd_select(struct timeval *);
void nd_shutdown(void);
#endif
