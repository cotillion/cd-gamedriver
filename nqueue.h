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
#ifndef _NQUEUE_H
#define _NQUEUE_H

typedef struct {
    u_short	nq_size;
    u_short	nq_len;
    u_short	nq_rptr;
    u_short	nq_wptr;
} nqueue_t;

void nq_init(nqueue_t *);
nqueue_t *nq_alloc(u_short);
void nq_free(nqueue_t *);
u_short nq_size(nqueue_t *);
u_short nq_len(nqueue_t *);
u_short nq_avail(nqueue_t *);
u_char *nq_rptr(nqueue_t *);
u_char *nq_wptr(nqueue_t *);
int nq_empty(nqueue_t *);
int nq_full(nqueue_t *);
u_char nq_getc(nqueue_t *);
void nq_ungetc(nqueue_t *, u_char);
void nq_putc(nqueue_t *, u_char);
void nq_puts(nqueue_t *, u_char *);
void nq_unputc(nqueue_t *);
int nq_recv(nqueue_t *, int, u_int *);
int nq_send(nqueue_t *, int, u_int *);
#endif
