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
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/telnet.h>
#include <netdb.h>
#include "config.h"
#include "patchlevel.h"
#include "lint.h"
#include "main.h"
#include "interpret.h"
#include "simulate.h"
#include "nqueue.h"
#include "ndesc.h"
#include "net.h"
#include "telnet.h"
#include "comm.h"
#include "object.h"
#include "backend.h"

#ifndef EPROTO
#define	EPROTO	EPROTOTYPE
#endif

/*
 * Telnet Server
 */

/*
 * Queue Sizes.
 */
#define	TELNET_CANQ_SIZE	512
#define	TELNET_RAWQ_SIZE	256
#define	TELNET_OPTQ_SIZE	512
#define	TELNET_OUTQ_SIZE	(16*1024 + 1024)

/*
 * Output Queue Flow Control Parameters.
 */
#define	TELNET_OUTQ_LOWAT       (TELNET_OUTQ_SIZE/2)
#define	TELNET_OUTQ_HIWAT       (TELNET_OUTQ_SIZE - 256)

/*
 * The following parameter specifies the minimum number of bytes that must
 * exist in the output queue to allow raw queue processing to continue.
 * It is set to the maximum number of bytes ever sent in response to a
 * TELNET command.  N.B. Do not change this unless you know what you
 * are doing!
 */
#define	TELNET_OUTQ_REQUIRED	12

/*
 * The folowing parameters are used to set the kernel socket buffer
 * size for the TELNET connection.  By default, BSD sets both send
 * and receive buffers to 4k.  It makes more sense to increase the
 * send size and reduce the receive size for a MUD.
 */
#define	TELNET_RCVBUF_SIZE	2048
#define	TELNET_SNDBUF_SIZE	TELNET_OUTQ_SIZE

/*
 * ASCII Definitions.
 */
#define	NUL			0
#define	BEL			7
#define	BS			8
#define	LF			10
#define	CR			13
#define	DEL			127

static void telnet_interactive(void *vp);
static void telnet_input(telnet_t *tp);
static void telnet_readbytes(ndesc_t *nd, telnet_t *tp);
static void telnet_send_sb(telnet_t *tp, u_char opt, u_char *data);


/*
 * Allocate a Telnet control block.
 */
static telnet_t *
telnet_alloc(void)
{
    int i;
    telnet_t *tp;

    tp = xalloc(sizeof (telnet_t));
    tp->t_flags = 0;
    tp->t_state = TS_DATA;
    tp->t_nd = NULL;
    tp->t_rawq = nq_alloc(TELNET_RAWQ_SIZE);
    tp->t_canq = nq_alloc(TELNET_CANQ_SIZE);
    tp->t_optq = NULL;
    tp->t_outq = nq_alloc(TELNET_OUTQ_SIZE);
    tp->t_ip = NULL;
    tp->t_rblen = 0;
    tp->t_sblen = 0;
    tp->task = NULL;

    for (i = 0; i < OP_SIZE; i++)
    {
        tp->t_optb[i].o_us = OS_NO;
        tp->t_optb[i].o_usq = OQ_EMPTY;
        tp->t_optb[i].o_him = OS_NO;
        tp->t_optb[i].o_himq = OQ_EMPTY;
    }

    return tp;
}

/*
 * Free a Telnet Control Block.
 */
static void
telnet_free(telnet_t *tp)
{
    nq_free(tp->t_rawq);
    nq_free(tp->t_canq);
    if (tp->t_optq != NULL)
        nq_free(tp->t_optq);
    nq_free(tp->t_outq);
    if (tp->task)
        remove_task(tp->task);
    tp->task = NULL;
    free(tp);
}

/*
 * Flush the output queue.
 */
static void
telnet_flush(telnet_t *tp)
{
    if (!nq_empty(tp->t_outq))
        nq_send(tp->t_outq, nd_fd(tp->t_nd), &tp->t_sblen);
}

/*
 * Close a Telnet session and free the associated resources.
 */
static void
telnet_shutdown(ndesc_t *nd, telnet_t *tp)
{
    if (tp != NULL)
    {
        telnet_flush(tp);
        telnet_free(tp);
    }

    (void)close(nd_fd(nd));
    nd_detach(nd);
}
/*
 * Associate a Telnet control block with an interactive object.
 */
static void
telnet_attach(telnet_t *tp, struct interactive *ip)
{
    tp->t_flags |= TF_ATTACH;
    tp->t_ip = ip;
}

/*
 * Disassociate a Telnet control block from an interactive object.
 */
void
telnet_detach(telnet_t *tp)
{
    if ((tp->t_flags & TF_ATTACH) == 0)
        return;

    tp->t_flags &= ~TF_ATTACH;
    tp->t_ip = NULL;
    if (!tp->task)
        tp->task = create_task(telnet_interactive, tp);
}

/*
 * Re-enable the output queue (if blocked).
 */
static void
telnet_enabw(telnet_t *tp)
{
    nd_enable(tp->t_nd, ND_W);
}

/*
 * Append a character string to the output queue, encapsulating it 
 * appropriately.
 * Returns 1 in case the message is truncated.
 */
int
telnet_output(telnet_t *tp, u_char *cp)
{
    int len;
    u_char c, *bp, buf[TELNET_OUTQ_SIZE + 3];

    if (tp->t_flags & TF_OVFLOUTQ)
        return 1;

    bp = buf;

    while (bp < &buf[TELNET_OUTQ_SIZE] && *cp != '\0')
    {
        c = *cp++;
        switch (c)
        {
            case LF:
                *bp++ = CR;
                break;

            case IAC:
                *bp++ = IAC;
                break;
        }
        *bp++ = c;
    }

    *bp = '\0';

    len = bp - buf;

    if (len == 0)
        return 0;

    if (nq_len(tp->t_outq) + len >= TELNET_OUTQ_HIWAT)
    {
        telnet_flush(tp);
        if (nq_len(tp->t_outq) + len >= TELNET_OUTQ_HIWAT)
        {
            tp->t_flags |= TF_OVFLOUTQ;
            buf[TELNET_OUTQ_HIWAT - nq_len(tp->t_outq)] = '\0';
            nq_puts(tp->t_outq, buf);
            nq_puts(tp->t_outq, (u_char *)"*** Truncated. ***\r\n");

            telnet_enabw(tp);
            return 1;
        }
    }

    nq_puts(tp->t_outq, buf);
    telnet_enabw(tp);
    return 0;
}

/* 
 * Send a GMCP SB message if GMCP has been enabled on the connection.
 * Returns 1 if gmcp is not enabled on the connection.
 */
int
telnet_output_gmcp(telnet_t *tp, u_char *cp)
{
    if (tp->t_flags & TF_GMCP)
    {
        telnet_send_sb(tp, TELOPT_GMCP, cp);
        return 0;
    }
    return 1;
}

/*
 * Notify an interactive object that the Telnet session has been disconnected.
 */
static void
telnet_disconnect(telnet_t *tp)
{
    if ((tp->t_flags & TF_ATTACH) == 0)
        return;

    tp->t_flags |= TF_DISCONNECT;
    if (!tp->task)
        tp->task = create_task(telnet_interactive, tp);
}

/*
 * Append a character to the canonical queue.
 */
static void
telnet_canq_putc(telnet_t *tp, u_char c)
{
    if (tp->t_flags & (TF_OVFLCANQ | TF_SYNCH))
        return;

    if (!nq_full(tp->t_canq))
    {
        nq_putc(tp->t_canq, c);
    }
    else
    {
        tp->t_flags |= TF_OVFLCANQ;
        if (nq_avail(tp->t_outq) > 0)
        {
            nq_putc(tp->t_outq, BEL);
            telnet_enabw(tp);
        }
    }
}

/*
 * Append a character to the option queue.
 */
static void
telnet_optq_putc(telnet_t *tp, u_char c)
{
    if (tp->t_flags & TF_OVFLOPTQ)
        return;

    if (!nq_full(tp->t_optq))
        nq_putc(tp->t_optq, c);
    else
        tp->t_flags |= TF_OVFLOPTQ;
}

/*
 * Send the contents of the canonical queue to the interactive object.
 */
static void
telnet_eol(telnet_t *tp)
{
    if (tp->t_flags & TF_SYNCH)
        return;

    if (nq_full(tp->t_canq))
    {
        tp->t_flags &= ~TF_OVFLCANQ;
    }
    else
    {
        nq_putc(tp->t_canq, '\0');
    }

    tp->t_flags |= TF_INPUT;
}

static void
telnet_interactive(void *vp)
{
    telnet_t *tp = vp;
    char *cp;
    if (!(tp->t_flags & TF_ATTACH)) {
        tp->task = NULL;
        telnet_shutdown(tp->t_nd, tp);
        return;
    }
    if (tp->t_flags & TF_DISCONNECT) {
        tp->t_flags &= ~TF_DISCONNECT;
        if (tp->t_ip)
            remove_interactive(tp->t_ip, 1);
    }
    if (!(tp->t_flags & TF_ATTACH)) {
        tp->task = NULL;
        telnet_shutdown(tp->t_nd, tp);
        return;
    }
    if (tp->t_flags & TF_OVFLOUTQ) {
        tp->task = NULL;
        return;
    }
    if (tp->t_flags & TF_INPUT) {
        tp->t_flags &= ~TF_INPUT;
        if (nq_full(tp->t_canq))
            cp = "";
        else
            cp = (char *)nq_rptr(tp->t_canq);
        interactive_input(tp->t_ip, cp);
        nq_init(tp->t_canq);
    }
    if (!(tp->t_flags & TF_ATTACH)) {
        tp->task = NULL;
        telnet_shutdown(tp->t_nd, tp);
        return;
    }
    tp->t_flags &= ~TF_GA;
    telnet_readbytes(tp->t_nd, tp);
    telnet_input(tp);
    if (!(tp->t_flags & TF_ATTACH)) {
        tp->task = NULL;
        telnet_shutdown(tp->t_nd, tp);
        return;
    }
    if (tp->t_flags & (TF_INPUT|TF_DISCONNECT)) {
        reschedule_task(tp->task);
        return;
    }
    tp->task = NULL;
    nd_enable(tp->t_nd, ND_R);
}

/*
 * Process a Data Mark.
 */
static void
telnet_dm(telnet_t *tp)
{
    if ((tp->t_flags & TF_URGENT) == 0)
        tp->t_flags &= ~TF_SYNCH;
}

/*
 * Process an Are You There.
 */
static void
telnet_ayt(telnet_t *tp)
{
    char version[24];

    snprintf(version, sizeof(version), "[%6.6s%02d]\r\n", GAME_VERSION, PATCH_LEVEL);

    nq_puts(tp->t_outq, (u_char *)version);

    telnet_enabw(tp);
}

/*
 * Process an Erase Character/Backspace/Delete.
 */
static INLINE void
telnet_ec(telnet_t *tp)
{
    if (tp->t_flags & (TF_OVFLCANQ | TF_SYNCH))
        return;

    if (!nq_empty(tp->t_canq))
        nq_unputc(tp->t_canq);
}

/*
 * Process an Erase Line.
 */
static void
telnet_el(telnet_t *tp)
{
    if (tp->t_flags & (TF_OVFLCANQ | TF_SYNCH))
        return;

    nq_init(tp->t_canq);
}

/*
 * Process a Subnegotiate Begin.
 */
static void
telnet_sb(telnet_t *tp, u_char opt)
{
    if (tp->t_optq == NULL)
        tp->t_optq = nq_alloc(TELNET_OPTQ_SIZE);

    tp->t_opt = opt;
}

/*
 * Process a Subnegotiate End.
 */
static void
telnet_se(telnet_t *tp)
{
    if (!nq_full(tp->t_optq) && !nq_empty(tp->t_optq))
    {
        if (tp->t_opt == TELOPT_GMCP) {
            nq_putc(tp->t_optq, '\0');
            gmcp_input(tp->t_ip, (char *)nq_rptr(tp->t_optq));
        }
    }

    tp->t_flags &= ~TF_OVFLOPTQ;
    nq_init(tp->t_optq);
}

/*
 * Return the option block pointer for the specified option.  If we have no
 * interest in ever negotiating the specified option, return NULL; i.e. it's
 * not one we know about, or we do but we'd never want to enable it.
 */
static opt_t *
telnet_get_optp(telnet_t *tp, u_char opt)
{
    switch (opt)
    {
        case TELOPT_ECHO:
            return &tp->t_optb[OP_ECHO];

        case TELOPT_SGA:
            return &tp->t_optb[OP_SGA];

        case TELOPT_CDM:
            return &tp->t_optb[OP_CDM];

        case TELOPT_GMCP:
            return &tp->t_optb[OP_GMCP];

        default:
            return NULL;
    }
}

/*
 * Enable an option in the local-to-remote direction?
 */
static int
telnet_lenabp(telnet_t *tp, u_char opt)
{
    switch (opt)
    {
        case TELOPT_ECHO:
            return 1;

        case TELOPT_SGA:
            return 1;

        case TELOPT_CDM:
            return 1;

        default:
            return 0;
    }
}

/*
 * Enable an option in the remote-to-local direction?
 */
static int
telnet_renabp(telnet_t *tp, u_char opt)
{
    switch (opt)
    {
        case TELOPT_SGA:
            return 1;

        default:
            return 0;
    }
}

/*
 * Send IAC GA.
 */
static void
telnet_send_ga(telnet_t *tp)
{
    nqueue_t *nq;

    nq = tp->t_outq;

    nq_putc(nq, IAC);
    nq_putc(nq, GA);
    telnet_enabw(tp);
}

/*
 * Send IAC WILL <option>.
 */
static void
telnet_send_will(telnet_t *tp, u_char opt)
{
    nqueue_t *nq;

    nq = tp->t_outq;

    nq_putc(nq, IAC);
    nq_putc(nq, WILL);
    nq_putc(nq, opt);
    telnet_enabw(tp);
}

/*
 * Send IAC WONT <option>.
 */
static void
telnet_send_wont(telnet_t *tp, u_char opt)
{
    nqueue_t *nq;

    nq = tp->t_outq;

    nq_putc(nq, IAC);
    nq_putc(nq, WONT);
    nq_putc(nq, opt);
    telnet_enabw(tp);
}

/*
 * Send IAC DO <option>.
 */
static void
telnet_send_do(telnet_t *tp, u_char opt)
{
    nqueue_t *nq;

    nq = tp->t_outq;

    nq_putc(nq, IAC);
    nq_putc(nq, DO);
    nq_putc(nq, opt);
    telnet_enabw(tp);
}

/*
 * Send IAC DONT <option>.
 */
static void
telnet_send_dont(telnet_t *tp, u_char opt)
{
    nqueue_t *nq;

    nq = tp->t_outq;

    nq_putc(nq, IAC);
    nq_putc(nq, DONT);
    nq_putc(nq, opt);
    telnet_enabw(tp);
}

/* 
 * Send IAC SB subnegotiation sequence
 */
static void
telnet_send_sb(telnet_t *tp, u_char opt, u_char *data)
{
    nqueue_t *nq;

    nq = tp->t_outq;

    nq_putc(nq, IAC);
    nq_putc(nq, SB);
    nq_putc(nq, opt);
    nq_puts(nq, data);
    nq_putc(nq, IAC);
    nq_putc(nq, SE);
    telnet_enabw(tp);
}

/*
 * Acknowledge enabling an option in the local-to-remote direction.
 */
static void
telnet_ack_lenab(telnet_t *tp, u_char opt)
{
    switch (opt)
    {
        case TELOPT_SGA:
            tp->t_flags |= TF_SGA;
            break;
        case TELOPT_GMCP:
            tp->t_flags |= TF_GMCP;
            break;
    }
}

/*
 * Acknowledge disabling an option in the local-to-remote direction.
 */
static void
telnet_ack_ldisab(telnet_t *tp, u_char opt)
{
    switch (opt)
    {
        case TELOPT_SGA:
            tp->t_flags &= ~TF_SGA;
            break;
    }
}

/*
 * Acknowledge enabling an option in the remote-to-local direction.
 */
static void
telnet_ack_renab(telnet_t *tp, u_char opt)
{
}

/*
 * Acknowledge disabling an option in the remote-to-local direction.
 */
static void
telnet_ack_rdisab(telnet_t *tp, u_char opt)
{
}

/*
 * Negotiate enabling an option in the local-to-remote direction.
 */
static void
telnet_neg_lenab(telnet_t *tp, u_char opt)
{
    opt_t *op;

    op = telnet_get_optp(tp, opt);
    if (op == NULL)
        return;

    switch (op->o_us)
    {
        case OS_NO:
            op->o_us = OS_WANTYES;
            telnet_send_will(tp, opt);
            break;

        case OS_YES:
            break;

        case OS_WANTNO:
            switch (op->o_usq)
            {
                case OQ_EMPTY:
                    op->o_usq = OQ_OPPOSITE;
                    break;

                case OQ_OPPOSITE:
                    break;
            }
            break;

        case OS_WANTYES:
            switch (op->o_usq)
            {
                case OQ_EMPTY:
                    break;

                case OQ_OPPOSITE:
                    op->o_usq = OQ_EMPTY;
                    break;
            }
            break;
    }
}

/*
 * Negotiate disabling an option in the local-to-remote direction.
 */
static void
telnet_neg_ldisab(telnet_t *tp, u_char opt)
{
    opt_t *op;

    op = telnet_get_optp(tp, opt);
    if (op == NULL)
        return;

    switch (op->o_us)
    {
        case OS_NO:
            break;

        case OS_YES:
            op->o_us = OS_WANTNO;
            telnet_send_wont(tp, opt);
            break;

        case OS_WANTNO:
            switch (op->o_usq)
            {
                case OQ_EMPTY:
                    break;

                case OQ_OPPOSITE:
                    op->o_usq = OQ_EMPTY;
                    break;
            }
            break;

        case OS_WANTYES:
            switch (op->o_usq)
            {
                case OQ_EMPTY:
                    op->o_usq = OQ_OPPOSITE;
                    break;

                case OQ_OPPOSITE:
                    break;
            }
            break;
    }
}

/*
 * Enable Remote Echo.
 */
void
telnet_enable_echo(telnet_t *tp)
{
    if (nq_avail(tp->t_outq) < 3)
        return;

    telnet_neg_lenab(tp, TELOPT_ECHO);
}

/*
 * Disable Remote Echo.
 */
void
telnet_disable_echo(telnet_t *tp)
{
    if (nq_avail(tp->t_outq) < 3)
        return;

    telnet_neg_ldisab(tp, TELOPT_ECHO);
}

/* 
 * Enable GMCP
 */
void
telnet_enable_gmcp(telnet_t *tp)
{
    if (nq_avail(tp->t_outq) < 3)
        return;

    telnet_neg_lenab(tp, TELOPT_GMCP);
}

/*
 * Disable GMCP
 */   
void
telnet_disable_gmcp(telnet_t *tp)
{
    if (nq_avail(tp->t_outq) < 3)
        return;

    telnet_neg_ldisab(tp, TELOPT_GMCP);
}


/*
 * Process IAC WILL <option>.
 */
static void
telnet_will(telnet_t *tp, u_char opt)
{
    opt_t *op;

    op = telnet_get_optp(tp, opt);
    if (op == NULL)
    {
        telnet_send_dont(tp, opt);
        return;
    }

    switch (op->o_him)
    {
        case OS_NO:
            if (telnet_renabp(tp, opt))
            {
                op->o_him = OS_YES;
                telnet_send_do(tp, opt);
                telnet_ack_renab(tp, opt);
            }
            else
            {
                telnet_send_dont(tp, opt);
            }
            break;

        case OS_YES:
            break;

        case OS_WANTNO:
            switch (op->o_himq)
            {
                case OQ_EMPTY:
                    op->o_him = OS_NO;
                    telnet_ack_rdisab(tp, opt);
                    break;

                case OQ_OPPOSITE:
                    op->o_him = OS_YES;
                    op->o_himq = OQ_EMPTY;
                    telnet_ack_renab(tp, opt);
                    break;
            }
            break;

        case OS_WANTYES:
            switch (op->o_himq)
            {
                case OQ_EMPTY:
                    op->o_him = OS_YES;
                    telnet_ack_renab(tp, opt);
                    break;

                case OQ_OPPOSITE:
                    op->o_him = OS_WANTNO;
                    op->o_himq = OQ_EMPTY;
                    telnet_send_dont(tp, opt);
            }
            break;
    }
}

/*
 * Process IAC WONT <option>.
 */
static void
telnet_wont(telnet_t *tp, u_char opt)
{
    opt_t *op;

    op = telnet_get_optp(tp, opt);
    if (op == NULL)
        return;

    switch (op->o_him)
    {
        case OS_NO:
            break;

        case OS_YES:
            op->o_him = OS_NO;
            telnet_send_dont(tp, opt);
            telnet_ack_rdisab(tp, opt);
            break;

        case OS_WANTNO:
            switch (op->o_himq)
            {
                case OQ_EMPTY:
                    op->o_him = OS_NO;
                    telnet_ack_rdisab(tp, opt);
                    break;

                case OQ_OPPOSITE:
                    op->o_him = OS_WANTYES;
                    op->o_himq = OQ_EMPTY;
                    telnet_send_do(tp, opt);
                    break;
            }
            break;

        case OS_WANTYES:
            switch (op->o_himq)
            {
                case OQ_EMPTY:
                    op->o_him = OS_NO;
                    telnet_ack_rdisab(tp, opt);
                    break;

                case OQ_OPPOSITE:
                    op->o_him = OS_NO;
                    op->o_himq = OQ_EMPTY;
                    telnet_ack_rdisab(tp, opt);
                    break;
            }
            break;
    }
}

/*
 * Process IAC DO <option>.
 */
static void
telnet_do(telnet_t *tp, u_char opt)
{
    opt_t *op;

    if (opt == TELOPT_TM)
    {
        telnet_send_will(tp, opt);
        return;
    }

    op = telnet_get_optp(tp, opt);
    if (op == NULL)
    {
        telnet_send_wont(tp, opt);
        return;
    }

    switch (op->o_us)
    {
        case OS_NO:
            if (telnet_lenabp(tp, opt))
            {
                op->o_us = OS_YES;
                telnet_send_will(tp, opt);
                telnet_ack_lenab(tp, opt);
            }
            else
            {
                telnet_send_wont(tp, opt);
            }
            break;

        case OS_YES:
            break;

        case OS_WANTNO:
            switch (op->o_usq)
            {
                case OQ_EMPTY:
                    op->o_us = OS_NO;
                    telnet_ack_ldisab(tp, opt);
                    break;

                case OQ_OPPOSITE:
                    op->o_us = OS_YES;
                    op->o_usq = OQ_EMPTY;
                    telnet_ack_lenab(tp, opt);
                    break;
            }
            break;

        case OS_WANTYES:
            switch (op->o_usq)
            {
                case OQ_EMPTY:
                    op->o_us = OS_YES;
                    telnet_ack_lenab(tp, opt);
                    break;

                case OQ_OPPOSITE:
                    op->o_us = OS_WANTNO;
                    op->o_usq = OQ_EMPTY;
                    telnet_send_wont(tp, opt);
            }
            break;
    }
}

/*
 * Process IAC DONT <option>.
 */
static void
telnet_dont(telnet_t *tp, u_char opt)
{
    opt_t *op;

    op = telnet_get_optp(tp, opt);
    if (op == NULL)
        return;

    switch (op->o_us)
    {
        case OS_NO:
            break;

        case OS_YES:
            op->o_us = OS_NO;
            telnet_send_wont(tp, opt);
            telnet_ack_ldisab(tp, opt);
            break;

        case OS_WANTNO:
            switch (op->o_usq)
            {
                case OQ_EMPTY:
                    op->o_us = OS_NO;
                    telnet_ack_ldisab(tp, opt);
                    break;

                case OQ_OPPOSITE:
                    op->o_us = OS_WANTYES;
                    op->o_usq = OQ_EMPTY;
                    telnet_send_will(tp, opt);
                    break;
            }
            break;

        case OS_WANTYES:
            switch (op->o_usq)
            {
                case OQ_EMPTY:
                    op->o_us = OS_NO;
                    telnet_ack_ldisab(tp, opt);
                    break;

                case OQ_OPPOSITE:
                    op->o_us = OS_NO;
                    op->o_usq = OQ_EMPTY;
                    telnet_ack_ldisab(tp, opt);
                    break;
            }
            break;
    }
}

/*
 * Perform Telnet protocol processing, copying the contents of the raw input
 * queue to the canonical queue.
 */
static void
telnet_input(telnet_t *tp)
{
    u_char c;
    while (!nq_empty(tp->t_rawq))
    {
        c = nq_getc(tp->t_rawq);

        switch (tp->t_state)
        {
            case TS_DATA:
                switch (c)
                {
                    case NUL:
                        break;

                    case BS:
                        telnet_ec(tp);
                        break;

                    case LF:
                        telnet_eol(tp);
                        return;

                    case CR:
                        tp->t_state = TS_CR;
                        break;

                    case DEL:
                        telnet_ec(tp);
                        break;

                    case IAC:
                        tp->t_state = TS_IAC;
                        break;

                    default:
                        telnet_canq_putc(tp, c);
                        break;
                }
                break;

            case TS_CR:
                telnet_eol(tp);
                tp->t_state = TS_DATA;
                return;

            case TS_IAC:
                tp->t_state = TS_DATA;
                switch (c)
                {
                    case DM:
                        telnet_dm(tp);
                        break;

                    case AYT:
                        telnet_ayt(tp);
                        break;

                    case EC:
                        telnet_ec(tp);
                        break;

                    case EL:
                        telnet_el(tp);
                        break;

                    case SB:
                        tp->t_state = TS_IAC_SB;
                        break;

                    case WILL:
                        tp->t_state = TS_IAC_WILL;
                        break;

                    case WONT:
                        tp->t_state = TS_IAC_WONT;
                        break;

                    case DO:
                        tp->t_state = TS_IAC_DO;
                        break;

                    case DONT:
                        tp->t_state = TS_IAC_DONT;
                        break;

                    case IAC:
                        telnet_canq_putc(tp, c);
                        break;
                }
                break;

            case TS_IAC_SB:
                telnet_sb(tp, c);
                tp->t_state = TS_IAC_SB_DATA;
                break;

            case TS_IAC_SB_DATA:
                switch (c)
                {
                    case IAC:
                        tp->t_state = TS_IAC_SB_IAC;
                        break;

                    default:
                        telnet_optq_putc(tp, c);
                        break;
                }
                break;

            case TS_IAC_SB_IAC:
                tp->t_state = TS_IAC_SB_DATA;
                switch (c)
                {
                    case SE:
                        telnet_se(tp);
                        tp->t_state = TS_DATA;
                        break;

                    case IAC:
                        telnet_optq_putc(tp, c);
                        break;
                }
                break;

            case TS_IAC_WILL:
                telnet_will(tp, c);
                tp->t_state = TS_DATA;
                break;

            case TS_IAC_WONT:
                telnet_wont(tp, c);
                tp->t_state = TS_DATA;
                break;

            case TS_IAC_DO:
                telnet_do(tp, c);
                tp->t_state = TS_DATA;
                break;

            case TS_IAC_DONT:
                telnet_dont(tp, c);
                tp->t_state = TS_DATA;
                break;
        }
    }

    if ((tp->t_flags & (TF_SGA | TF_GA)) == 0)
    {
        if (nq_avail(tp->t_outq) >= 2)
        {
            tp->t_flags |= TF_GA;
            telnet_send_ga(tp);
        }
    }

    nq_init(tp->t_rawq);
}

/*
 * Read data from the Telnet session to the raw input queue.
 */
static void
telnet_readbytes(ndesc_t *nd, telnet_t *tp)
{
    int cc;

    if (!nq_full(tp->t_rawq))
    {
        if (tp->t_flags & TF_URGENT)
        {
            if (at_mark(nd_fd(nd)))
            {
                tp->t_flags &= ~TF_URGENT;
                nd_enable(nd, ND_X);
            }
        }

        cc = nq_recv(tp->t_rawq, nd_fd(nd), &tp->t_rblen);
        if (cc == -1)
        {
            switch (errno)
            {
                case EWOULDBLOCK:
                case EINTR:
                case EPROTO:
                    break;

                default:
                    telnet_disconnect(tp);
                    return;
            }
        }

        if (cc == 0)
        {
            telnet_disconnect(tp);
            return;
        }

        if (!nq_full(tp->t_rawq))
            return;
    }

}

static void
telnet_read(ndesc_t *nd, telnet_t *tp)
{
    telnet_readbytes(nd, tp);
    telnet_input(tp);
    if (tp->t_flags & (TF_INPUT|TF_DISCONNECT)) {
        if (!tp->task)
            tp->task = create_task(telnet_interactive, tp);
        nd_disable(tp->t_nd, ND_R);
    }
}


/*
 * Write data from the output queue to the Telnet session.
 */
static void
telnet_write(ndesc_t *nd, telnet_t *tp)
{
    if (!nq_empty(tp->t_outq))
    {
        if (nq_send(tp->t_outq, nd_fd(nd), &tp->t_sblen) == -1)
        {
            switch (errno)
            {
                case EWOULDBLOCK:
                case EINTR:
                case EPROTO:
                    break;

                default:
                    telnet_disconnect(tp);
                    return;
            }
        }
    }

    if (tp->t_flags & TF_OVFLOUTQ)
    {
        if (nq_len(tp->t_outq) < TELNET_OUTQ_LOWAT)
        {
            tp->t_flags &= ~TF_OVFLOUTQ;
            if (tp->t_flags & (TF_INPUT|TF_DISCONNECT) &&
                !tp->task) /* Reenable command processing */
                tp->task = create_task(telnet_interactive, tp);
        }
    }

    if (!nq_empty(tp->t_outq))
        return;

    nq_init(tp->t_outq);

    nd_disable(nd, ND_W);
}

/*
 * Process an Urgent Data indication.
 */
static void
telnet_exception(ndesc_t *nd, telnet_t *tp)
{
    tp->t_flags |= TF_SYNCH | TF_URGENT;
    nq_init(tp->t_canq);
    nd_disable(nd, ND_X);
}


/*
 * Accept a new Telnet connection.
 */
static void
telnet_accept(void *vp)
{
    ndesc_t *nd = vp;
    int s;
    u_short local_port;

    socklen_t addrlen;
    struct sockaddr_storage addr;
    telnet_t *tp;
    void *ip;
    char host[NI_MAXHOST], port[NI_MAXSERV];

    nd_enable(nd, ND_R);

    /* Get the port number of the accepting socket */
    addrlen = sizeof(addr);
    if (getsockname(nd_fd(nd), (struct sockaddr *)&addr, &addrlen))
        return;

    getnameinfo((struct sockaddr *)&addr, addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    local_port = atoi(port);

    addrlen = sizeof (addr);
    s = accept(nd_fd(nd), (struct sockaddr *)&addr, &addrlen);
    if (s == -1)
    {
        switch (errno)
        {
            default:
                getnameinfo((struct sockaddr *)&addr, addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);

                warning("telnet_accept: accept() errno = %d. ip: [%s]:%s\n",
                        errno, host, port);
            case EWOULDBLOCK:
            case EINTR:
            case EPROTO:
                return;
        }
    }

    enable_nbio(s);
    enable_oobinline(s);
    enable_nodelay(s);
    enable_lowdelay(s);
    enable_keepalive(s);
    set_rcvsize(s, TELNET_RCVBUF_SIZE);
    set_sndsize(s, TELNET_SNDBUF_SIZE);

    tp = telnet_alloc();
    tp->t_nd = nd_attach(s, telnet_read, telnet_write, telnet_exception,
                         NULL, telnet_shutdown, tp);

    /* Start negotiation of GMCP */
    telnet_enable_gmcp(tp);

    ip = (void *)new_player(tp, &addr, addrlen, local_port);
    if (ip == NULL)
    {
        telnet_shutdown(tp->t_nd, tp);
    }
    else
    {
        telnet_attach(tp, ip);
        nd_enable(tp->t_nd, ND_R | ND_X);
    }
}

static void
telnet_ready(ndesc_t *nd, void *vp)
{
    nd_disable(nd, ND_R);
    create_task(telnet_accept, nd);
}

/*
 * Initialize the Telnet server.
 */
void
telnet_init(u_short port_nr)
{
    int s = -1, e;
    struct addrinfo hints;
    struct addrinfo *res, *rp;    
    ndesc_t *nd;
    char host[NI_MAXHOST], port[NI_MAXSERV];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port, sizeof(port), "%d", port_nr);    
    if ((e = getaddrinfo(NULL, port, &hints, &res)))
        fatal("telnet_init: %s\n", gai_strerror(e));

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        if ((s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1)
            fatal("telnet_init: socket() error = %d.\n", errno);

        getnameinfo(rp->ai_addr, rp->ai_addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);

        enable_reuseaddr(s);

        if (rp->ai_family == AF_INET6)
            enable_v6only(s);

        if (bind(s, rp->ai_addr, rp->ai_addrlen) == 0)
        {
            /* Success */
            printf("Listening to telnet port: %s:%s\n",  host, port);
            enable_nbio(s);

            if (listen(s, 5) == -1)
                fatal("telnet_init: listen() error = %d.\n", errno);

            nd = nd_attach(s, telnet_ready, NULL, NULL, NULL, telnet_shutdown, NULL);
            nd_enable(nd, ND_R);

        }
        else
        {
            if (errno == EADDRINUSE)
            {
                fatal("Telnet socket already bound: %s:%s\n", host, port);
            }
            else
            {
                fatal("telnet_init: bind() error = %d.\n", errno);
            }
        }
    }
}
