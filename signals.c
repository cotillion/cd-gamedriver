#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "config.h"
#include "lint.h"
#include "interpret.h"
#include "backend.h"

void init_signals(void);
void deliver_signals(void);

static struct sig_disp
{
    int sig;
    char *name;
} disp[] = {
    {SIGCHLD, "CHLD"},
    {SIGHUP, "HUP"},
    {SIGINT, "INT"},
    {SIGQUIT, "QUIT"},
    {SIGTERM, "TERM"},
    {SIGUSR1, "USR1"},
    {SIGUSR2, "USR2"},
    {SIGTSTP, "TSTP"},
    {SIGCONT, "CONT"},
    {0, "UNKNOWN"},
};

#define MAX_SIGNALS (sizeof(disp)/sizeof(*disp))

static long long pending_signals = 0;
static sigset_t sigs_to_block;

static void
sig_handler(int sig)
{
    int i;

    /* When this happens our hname child has died
     * and neads to be reaped */
    if (sig == SIGCHLD)
    {
        waitpid(-1, NULL, WNOHANG);
    }

    for (i = 0; disp[i].sig && disp[i].sig != sig; i++)
	;
    
    pending_signals |= 1 << i;
}

void
init_signals()
{
    int i;
    struct sigaction act;

    sigemptyset(&sigs_to_block);
    for (i = 0; disp[i].sig; i++)
        sigaddset(&sigs_to_block, disp[i].sig);

    act.sa_handler = sig_handler;
    act.sa_mask = sigs_to_block;
    act.sa_flags = SA_RESTART;
    for (i = 0; disp[i].sig; i++)
	(void)sigaction(disp[i].sig, &act, NULL);
}

static struct task *signal_task;

static void
send_signals(void *x)
{
    int i;
    
    sigprocmask(SIG_BLOCK, &sigs_to_block, NULL);
    for (i = 0; i < MAX_SIGNALS; i++)
	if (pending_signals & (1 << i))
            break;

    pending_signals &= ~(1 << i);
    sigprocmask(SIG_UNBLOCK, &sigs_to_block, NULL);

    if (pending_signals)
        reschedule_task(signal_task);
    else
        signal_task = 0;
    if (i < MAX_SIGNALS)
        push_string(disp[i].name, STRING_CSTRING);
        (void)apply_master_ob(M_EXTERNAL_SIGNAL, 1);
}

void
deliver_signals()
{
    if (signal_task || !pending_signals)
        return;

    signal_task = create_task(send_signals, 0);
    return;
}
