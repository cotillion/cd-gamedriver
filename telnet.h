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

/*
 * Telnet Option Block.
 */
typedef struct {
    u_char	o_us;
    u_char	o_usq;
    u_char	o_him;
    u_char	o_himq;
} opt_t;

/*
 * Option States.
 */
#define	OS_NO			0
#define	OS_YES			1
#define	OS_WANTNO		2
#define	OS_WANTYES		3

/*
 * Option Queue States.
 */
#define	OQ_EMPTY		0
#define	OQ_OPPOSITE		2

/*
 * Options.
 */
#define	OP_ECHO			0
#define	OP_SGA			1
#define	OP_CDM			2
#define OP_GMCP         3 
#define OP_SIZE         4

/*
 * Telnet Control Block.
 */
typedef struct {
    u_short	t_flags;
    u_char	t_state;
    u_char	t_opt;
    opt_t	t_optb[OP_SIZE];
    ndesc_t *	t_nd;
    nqueue_t *	t_rawq;
    nqueue_t *	t_canq; nqueue_t *	t_optq;
    nqueue_t *	t_outq;
    void *	t_ip;
    u_int	t_rblen;
    u_int	t_sblen;
    struct task *task;
} telnet_t;

/*
 * Telnet Flags.
 */
#define	TF_ATTACH		0x0001
#define TF_INPUT        0x0002
#define TF_DISCONNECT   0x0004

#define TF_OVFLCANQ		0x0010
#define TF_OVFLOPTQ		0x0020
#define	TF_OVFLOUTQ		0x0040
#define TF_SYNCH		0x0080
#define TF_URGENT		0x0100
#define	TF_GA			0x0200
#define	TF_ECHO			0x1000
#define	TF_SGA			0x2000
#define TF_GMCP         0x4000


/*
 * Telnet Input States.
 */
#define	TS_DATA			0
#define	TS_CR			1
#define	TS_IAC			2
#define	TS_IAC_SB		3
#define	TS_IAC_SB_DATA		4
#define	TS_IAC_SB_IAC		5
#define	TS_IAC_WILL		6
#define	TS_IAC_WONT		7
#define	TS_IAC_DO		8
#define	TS_IAC_DONT		9

#define TELOPT_GMCP     201
#define	TELOPT_CDM		205

void telnet_detach(telnet_t *);
int  telnet_output(telnet_t *, u_char *);
int  telnet_output_gmcp(telnet_t *, u_char *);
void telnet_enable_echo(telnet_t *);
void telnet_disable_echo(telnet_t *);
void telnet_enable_gmcp(telnet_t *);
void telnet_disable_gmcp(telnet_t *);
void telnet_init(u_short);
