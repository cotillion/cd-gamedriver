/*-
 * Copyright (c) 1996 Dave Richards <dave@synergy.org>
 * Copyright (c) 1996 Thorsten Lockert <tholo@sigmasoft.com>
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

struct mstring_hdr {
#ifdef DEBUG
	u_long	magic;
#endif
	u_int	length;
	u_short	count;
};

#define	mstring_header \
	sizeof (struct mstring_hdr)

#ifdef DEBUG

#define	MSTRING_MAGIC	0x12311943

#define	mstring_magic(cp) \
	(((struct mstring_hdr *)((char *)(cp) - mstring_header))->magic)

#endif

#define	mstring_len(cp) \
	(((struct mstring_hdr *)((char *)(cp) - mstring_header))->length)

#define	mstring_count(cp) \
	(((struct mstring_hdr *)((char *)(cp) - mstring_header))->count)

char *allocate_mstring(size_t);
char *make_mstring(const char *);
char *reference_mstring(char *);
void free_mstring(char *);

struct sstring_hdr {
#ifdef DEBUG
	u_long	magic;
#endif
	char	*prev;
	char	*next;
	u_int	length;
	u_short	count;
	u_short	hash;
};

#define	sstring_header \
	sizeof (struct sstring_hdr)

#ifdef DEBUG

#define	SSTRING_MAGIC	0x04301963

#define	sstring_magic(cp) \
	(((struct sstring_hdr *)((char *)(cp) - sstring_header))->magic)

#endif

#define	sstring_prev(cp) \
	(((struct sstring_hdr *)((char *)(cp) - sstring_header))->prev)

#define	sstring_next(cp) \
	(((struct sstring_hdr *)((char *)(cp) - sstring_header))->next)

#define	sstring_len(cp) \
	(((struct sstring_hdr *)((char *)(cp) - sstring_header))->length)

#define	sstring_count(cp) \
	(((struct sstring_hdr *)((char *)(cp) - sstring_header))->count)

#define	sstring_hash(cp) \
	(((struct sstring_hdr *)((char *)(cp) - sstring_header))->hash)

char *reference_sstring(char *);
char *make_sstring(const char *);
void free_sstring(char *);
char *find_sstring(char *);

#define	HASH_SSTRING(cp)	(hashstr16((cp), 64) % HTABLE_SIZE)

#ifdef DEBUG
void dump_sstrings(void);
#endif
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void remove_string_hash(void);
#endif
