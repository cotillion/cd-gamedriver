/* Build with function sections so this exercises the production queue writer
 * without a network listener or unrelated driver dependencies. */
#include <assert.h>
#include <stdlib.h>
#include "../telnet.c"

void *xalloc(size_t size) { void *p = malloc(size); assert(p); return p; }
int nd_fd(ndesc_t *nd) { (void)nd; return -1; }
void nd_enable(ndesc_t *nd, int mask) { (void)nd; (void)mask; }

int
main(void)
{
    telnet_t tp = { 0 };
    mssp_t *var = malloc(sizeof(*var) + sizeof(u_char *));
    mssp_t *vars[] = { var };
    u_char value[] = { 'a', IAC, 'b', 0 };
    u_char expected[] = { IAC, SB, TELOPT_MSSP, MSSP_VAR, 'X',
                         MSSP_VAL, 'a', IAC, IAC, 'b', IAC, SE };
    u_char huge[TELNET_OUTQ_SIZE + 1];
    unsigned int before;

    assert(var);
    tp.t_outq = nq_alloc(TELNET_OUTQ_SIZE);
    var->name = (u_char *)"X";
    var->size = 1;
    var->values[0] = value;
    assert(telnet_output_mssp(&tp, vars, 1));
    assert(nq_len(tp.t_outq) == sizeof(expected));
    for (size_t i = 0; i < sizeof(expected); ++i)
        assert(nq_getc(tp.t_outq) == expected[i]);

    /* Repeated negotiation without draining simulates output backpressure. */
    while (telnet_output_mssp(&tp, vars, 1))
        assert(nq_len(tp.t_outq) <= TELNET_OUTQ_HIWAT);
    before = nq_len(tp.t_outq);
    for (int i = 0; i < 100000; ++i)
        assert(!telnet_output_mssp(&tp, vars, 1));
    assert(nq_len(tp.t_outq) == before);
    assert(telnet_output(&tp, (u_char *)"ordinary output after MSSP") == 1);
    assert(nq_len(tp.t_outq) <= nq_size(tp.t_outq));
    nq_init(tp.t_outq);
    memset(huge, 'x', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = 0;
    var->values[0] = huge;
    assert(!telnet_output_mssp(&tp, vars, 1));
    assert(nq_empty(tp.t_outq));
    /* An exactly fitting response is valid, including its closing IAC SE. */
    huge[TELNET_OUTQ_HIWAT - 8] = 0;
    assert(telnet_output_mssp(&tp, vars, 1));
    assert(nq_len(tp.t_outq) == TELNET_OUTQ_HIWAT);
    assert(!telnet_output_mssp(&tp, vars, 1));
    nq_init(tp.t_outq);
    memset(huge, IAC, 20000);
    huge[20000] = 0;
    assert(!telnet_output_mssp(&tp, vars, 1));
    assert(nq_empty(tp.t_outq));

    /* Drain to the end of the ring, then write a response across the wrap. */
    for (int i = 0; i < TELNET_OUTQ_SIZE - 3; ++i)
        nq_putc(tp.t_outq, 'x');
    while (!nq_empty(tp.t_outq))
        (void)nq_getc(tp.t_outq);
    var->values[0] = value;
    assert(telnet_output_mssp(&tp, vars, 1));
    for (size_t i = 0; i < sizeof(expected); ++i)
        assert(nq_getc(tp.t_outq) == expected[i]);
    assert(nq_empty(tp.t_outq));
    nq_free(tp.t_outq);
    free(var);
    return 0;
}
