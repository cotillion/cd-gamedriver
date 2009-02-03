#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/times.h>
#include <math.h>
#include <memory.h>
#include <unistd.h>
#include <sys/time.h>

#include "config.h"
#include "lint.h"
#include "interpret.h"
#include "ndesc.h"
#include "object.h"
#include "exec.h"
#include "comm.h"
#include "mudstat.h"
#include "super_snoop.h"
#include "signals.h"
#include "comm1.h"
#include "simulate.h"
#include "backend.h"
#include "mstring.h"

struct gdexception *exception = NULL;
/*
 * The driver's idea of current time
 */

extern struct object *command_giver, *current_interactive, *obj_list_destruct;
extern int num_player, d_flag, s_flag;
extern struct object *previous_ob, *master_ob;

extern int slow_shut_down_to_do;

void tmpclean (void);
void prepare_ipc(void),
    shutdowngame(void), ed_cmd (char *), call_out(struct timeval *),
    destruct2 (struct object *);

extern int player_parser (char *),
    call_function_interactive (struct interactive *, char *);

extern int t_flag;

/*
 * There are global variables that must be zeroed before any execution.
 * In case of errors, there will be a longjmp(), and the variables will
 * have to be cleared explicitely. They are normally maintained by the
 * code that use them.
 *
 * This routine must only be called from top level, not from inside
 * stack machine execution (as stack will be cleared).
 */
void 
clear_state(void) 
{
    extern struct object *previous_ob;

    current_object = 0;
    command_giver = 0;
    current_interactive = 0;
    previous_ob = 0;
    current_prog = 0;
    reset_machine();	/* Pop down the stack. */
}

void 
logon(struct object *ob)
{
    struct svalue *ret;
    struct object *save = current_object;

    /*
     * current_object must be set here, so that the static "logon" in
     * player.c can be called.
     */
    current_object = ob;
    ret = apply("logon", ob, 0, 0);
    if (ret == 0) {
	(void)add_message("prog %s:\n", ob->name);
	fatal("Could not find logon on the player %s\n", ob->name);
    }
    current_object = save;
}

/*
 * Take a player command and parse it.
 * The command can also come from a NPC.
 * Beware that 'str' can be modified and extended !
 */
int 
parse_command(char *str, struct object *ob)
{
    struct object *save = command_giver;
    int res;

    command_giver = ob;
    res = player_parser(str);
    command_giver = save;
    return res;
}



/*
 * Temporary malloc, for memory allocated during one run in the backend loop
 * NOTE
 *   Memory allocated with tmpalloc() MUST be temprary because it is freed
 *   at the beginning of the backend() loop.
 *
 * Thanks to Marcus J Ranum (mjr@decuac.DEC.COM) for the idea.
 */
static 	char	**tmpmem = 0;
static 	size_t	tmp_size = 0;
static 	int	tmp_cur = 0;

#define TMPMEM_CHUNK 1024

void 
tmpclean(void)
{
    int il;

    for (il = tmp_cur - 1; il >=0; il--) /* Reversed order is probably good */
	free(tmpmem[il]);
    tmp_cur = 0;
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
    if (tmp_size > 0)
    {
	free(tmpmem);
	tmpmem = NULL;
	tmp_size = 0;
    }
#endif
}

void *
tmpalloc(size_t size)
{
    char  **list;

    if (tmp_cur >= tmp_size)
    {
	list = (char **)xalloc((tmp_size + TMPMEM_CHUNK) * sizeof(char *));

	if (tmp_size > 0)
	{
	    (void)memcpy(list, tmpmem, tmp_size * sizeof(char *));
	    free((char *)tmpmem);
	}

	tmpmem = list;
	tmp_size += TMPMEM_CHUNK;
    }
    return tmpmem[tmp_cur++] = xalloc(size);
}

char *
tmpstring_copy(char *cp)
{
    char *xp;

    xp = tmpalloc(strlen(cp) + 1);
    (void)strcpy(xp, cp);
    return xp;
}

int eval_cost;

static void
sigfpe_handler(int sig)
{
#ifdef SYSV
    signal(SIGFPE, sifgpe_handler);
#endif
    error("Floating point error.\n");
}

static struct task task_head;
static long num_tasks = 0;

void
init_tasks(void) {
    task_head.fkn = task_head.arg = 0;
    task_head.prev = task_head.next = &task_head;
}

static void 
append_task(struct task *task) {
    if (!task)
	return;
    task->next = &task_head;
    task->prev = task_head.prev;
    task_head.prev->next = task;
    task_head.prev = task;
    num_tasks++;
}

static void
prepend_task(struct task *task)
{
    task->next = task_head.next;
    task->prev = &task_head;
    task_head.next->prev = task;
    task_head.next = task;
    num_tasks++;
}

struct task *
create_task(void (*f)(void *), void *arg)
{
    struct task *new_task;
    
    new_task = xalloc(sizeof(struct task));
    new_task->fkn = f;
    new_task->arg = arg;
    append_task(new_task);
    return new_task;
}

struct task *
create_hiprio_task(void (*f)(void *), void *arg)
{
    struct task *new_task;
    
    new_task = xalloc(sizeof(struct task));
    new_task->fkn = f;
    new_task->arg = arg;
    prepend_task(new_task);
    return new_task;
}

    
void
remove_task(struct task*t)
{
    if (!t)
	return;
    if (t->next && t->next != t)
	num_tasks--;
    if (t->prev)
	t->prev->next = t->next;
    if (t->next)
	t->next->prev = t->prev;

    t->prev = t->next = 0;
}

void
reschedule_task(struct task *t)
{
    if (!t)
	return;
    
    if (t->next == t)
	return; /* It's the currently running task, it will be rescheduled
		   when it is finnished */
    if (t->next == 0) {
	t->prev = t->next = t; /* it's the currrently running task. Mark it
				  for rescheduling */
	return;
    }
    /* Move the task to the tail */
    remove_task(t);
    append_task(t);
}

static void
runtask(struct task *t)
{
    struct gdexception exception_frame;
    
    exception_frame.e_exception = NULL;
    exception_frame.e_catch = 0;
    exception = &exception_frame;
    clear_state();
    tmpclean(); 		/* Free all temporary memory */
    remove_destructed_objects(); /* marion - before ref checks! */
    eval_cost = 0;

    if (setjmp(exception_frame.e_context) == 0) {
	t->fkn(t->arg);
    }
    clear_state();
    tmpclean(); 		/* Free all temporary memory */
    remove_destructed_objects(); /* marion - before ref checks! */
    eval_cost = 0;
}

/*
 * Call this one when there is only little memory left.
 * We tell master object of our troubles and hope it does something
 * intelligent, like starting Armageddon.
 */

static struct task *shutdown_task;

static void 
slow_shut_down(void *v)
{
    shutdown_task = 0;
    push_number(slow_shut_down_to_do);
    slow_shut_down_to_do = 0;
    apply_master_ob(M_MEMORY_FAILURE, 1);
}

static void
check_for_slow_shut_down(void)
{
    if (!slow_shut_down_to_do || shutdown_task) 
	return;
    shutdown_task = create_hiprio_task(slow_shut_down, 0);
}

void
mainloop(void)
{
    extern int game_is_being_shut_down;
    struct task *current_task;
    struct timeval tv;
    double task_start;

#ifdef SUPER_SNOOP
    read_snoop_file();
#endif
    (void) printf("Setting up ipc.\n");
    (void)fflush(stdout);
    prepare_ipc();
    (void) signal(SIGFPE, sigfpe_handler);

    while (!game_is_being_shut_down) {
	while (task_head.next == &task_head) {
	    set_current_time();
	    deliver_signals();
	    call_out(&tv);	    
	    if (task_head.next != &task_head)
		tv.tv_sec = tv.tv_usec = 0;
	    nd_select(&tv);
	    check_for_slow_shut_down();
	}
	set_current_time();
	current_task = task_head.next;
	remove_task(current_task);
	runtask(current_task);
	task_start = current_time;
	set_current_time();
	update_runq_av((num_tasks + 1.0) * (current_time - task_start));

	/* process callouts and IO */
	deliver_signals();
	if (task_head.next != &task_head ||
	    current_task->next == current_task) {
	    tv.tv_sec = tv.tv_usec = 0;
	    call_out(NULL);
        } else 
	    call_out(&tv);
	if (task_head.next != &task_head ||
	    current_task->next == current_task)
	    tv.tv_sec = tv.tv_usec = 0;
	nd_select(&tv);
	check_for_slow_shut_down();
	
	if (current_task->next == current_task)
	    append_task(current_task); /* reschedule the task */
	else
	    free(current_task);
    }
    shutdowngame();
}

/*
 * The start_boot() in master.c is supposed to return an array of files to load.
 * The array returned by apply() will be freed at next call of apply(),
 * which means that the ref count has to be incremented to protect against
 * deallocation.
 *
 * The master object is asked to do the actual loading.
 */
void 
preload_objects(int eflag)
{
    struct gdexception exception_frame;
    struct vector *prefiles;
    struct svalue *ret = NULL;
    volatile int ix;

    set_current_time();


    if (setjmp(exception_frame.e_context)) 
    {
	clear_state();
	(void)add_message("Error in start_boot() in master_ob.\n");
	exception = NULL;
	return;
    }
    else
    {
	exception_frame.e_exception = NULL;
	exception_frame.e_catch = 0;
	exception = &exception_frame;
	push_number(eflag);
	ret = apply_master_ob(M_START_BOOT, 1);
    }

    if ((ret == 0) || (ret->type != T_POINTER))
	return;
    else
	prefiles = ret->u.vec;

    if ((prefiles == 0) || (prefiles->size < 1))
	return;

    INCREF(prefiles->ref); /* Otherwise it will be freed next sapply */

    ix = -1;
    if (setjmp(exception_frame.e_context)) 
    {
	clear_state();
	(void)add_message("Anomaly in the fabric of world space.\n");
    }

    while (++ix < prefiles->size) 
    {
        set_current_time();
	if (s_flag)
	    reset_mudstatus();
	eval_cost = 0;
	push_svalue(&(prefiles->item[ix]));
	(void)apply_master_ob(M_PRELOAD_BOOT, 1);
	if (s_flag)
	    print_mudstatus(prefiles->item[ix].u.string, eval_cost,
			    get_millitime(), get_processtime());
	tmpclean();
    }
    free_vector(prefiles);
    exception = NULL;
    set_current_time();
}

/*
 * All destructed objects are moved int a sperate linked list,
 * and deallocated after program execution.
 */
void 
remove_destructed_objects(void)
{
    struct object *ob, *next;
    for (ob = obj_list_destruct; ob; ob = next)
    {
	next = ob->next_all;
	destruct2(ob);
    }
    obj_list_destruct = 0;
}

/*
 * Append string to file. Return 0 for failure, otherwise 1.
 */
int 
write_file(char *file, char *str)
{
    FILE *f;

    file = check_valid_path(file, current_object, "write_file", 1);

    if (!file)
	return 0;
    f = fopen(file, "a");
    if (f == 0)
	error("Wrong permissions for opening file %s for append.\n", file);
    if (s_flag)
	num_filewrite++;
    if (fwrite(str, strlen(str), 1, f) != 1) {
	(void)fclose(f);
	return 0;
    }
    if (fclose(f) == EOF)
	return 0;
    return 1;
}

int read_file_len; /* Side effect from read_file, so we know how many lines
		      we managed to read */
char *
read_file(char *file, int start, int len)
{
    struct stat st;
    FILE *f;
    char *str, *p, *p2, *end, c;
    size_t size;

    read_file_len = len;
    if (len < 0) return 0;

    file = check_valid_path(file, current_object, "read_file", 0);

    if (!file)
	return 0;
    f = fopen(file, "r");
    if (f == 0)
	return 0;

#ifdef PURIFY
    (void)memset(&st, '\0', sizeof(st));
#endif

    if (fstat(fileno(f), &st) == -1)
	fatal("Could not stat an open file.\n");
    size = (int)st.st_size;
    if (s_flag)
	num_fileread++;
    if (size > READ_FILE_MAX_SIZE)
    {
	if ( start || len )
	    size = READ_FILE_MAX_SIZE;
	else
	{
	    (void)fclose(f);
	    return 0;
	}
    }
    if (!start)
	start = 1;
    if (!len)
	read_file_len = len = READ_FILE_MAX_SIZE;
    str = allocate_mstring(size);
    str[size] = '\0';
    do
    {
	if (size > (int)st.st_size)
	    size = (int)st.st_size;
        if (fread(str, size, 1, f) != 1)
	{
    	    (void)fclose(f);
	    free_mstring(str);
    	    return 0;
        }
	st.st_size -= size;
	end = str + size;
        for (p = str; ( p2 = memchr(p, '\n', (size_t)(end - p)) ) && --start; )
	    p = p2 + 1;
    } while ( start > 1 );
    
    for (p2 = str; p != end; )
    {
        c = *p++;
	if (!isprint(c) && !isspace(c))
	    c = ' ';
	*p2++ = c;
	if ( c == '\n' )
	    if (!--len)
		break;
    }
    if (len && st.st_size)
    {
	size -= (p2 - str) ; 
	if (size > (int)st.st_size)
	    size = (int)st.st_size;
        if (fread(p2, size, 1, f) != 1)
	{
    	    (void)fclose(f);
	    free_mstring(str);
    	    return 0;
        }
	st.st_size -= size;
	end = p2 + size;
        for (; p2 != end; )
	{
	    c = *p2;
	    if (!isprint(c) && !isspace(c))
		*p2 = ' ';
	    p2++;
	    if (c == '\n')
	        if (!--len) break;
	}
	if ( st.st_size && len )
	{
	    /* tried to read more than READ_MAX_FILE_SIZE */
	    (void)fclose(f);
	    free_mstring(str);
	    return 0;
	}
    }
    read_file_len -= len;
    *p2 = '\0';
    (void)fclose(f);
    return str;
}

char *
read_bytes(char *file, int start, size_t len)
{
    struct stat st;

    char *str;
    int size;
    int f;

    if(len > MAX_BYTE_TRANSFER)
	return 0;

    file = check_valid_path(file, current_object, "read_bytes", 0);

    if (!file)
	return 0;
    f = open(file, O_RDONLY);
    if (f < 0)
	return 0;

#ifdef PURIFY
    (void)memset(&st, '\0', sizeof(st));
#endif

    if (fstat(f, &st) == -1)
	fatal("Could not stat an open file.\n");
    size = (int)st.st_size;
    if(start < 0) 
	start = size + start;

    if (start >= size)
    {
	(void)close(f);
	return 0;
    }
    if ((start+len) > size) 
	len = (size - start);

    if ((size = (int)lseek(f, (off_t)start, 0)) < 0)
    {
	(void)close(f);
	return 0;
    }

    str = allocate_mstring(len);

    size = read(f, str, len);

    (void)close(f);

    if (size <= 0)
    {
        free_mstring(str);
	return 0;
    }

    /* We want to allow all characters to pass untouched!
    for (il = 0; il < size; il++) 
	if (!isprint(str[il]) && !isspace(str[il]))
	    str[il] = ' ';

    */
    /*
     * The string has to end to '\0'!!!
     */
    str[size] = '\0';


    return str;
}

int
write_bytes(char *file, int start, char *str)
{
    struct stat st;

    int size, f;

    file = check_valid_path(file, current_object, "write_bytes", 1);

    if (!file)
	return 0;
    if (strlen(str) > MAX_BYTE_TRANSFER)
	return 0;
    f = open(file, O_WRONLY);
    if (f < 0)
	return 0;

#ifdef PURIFY
    (void)memset(&st, '\0', sizeof(st));
#endif

    if (fstat(f, &st) == -1)
	fatal("Could not stat an open file.\n");
    size = (int)st.st_size;
    if(start < 0) 
	start = size + start;

    if (start >= size)
    {
	(void)close(f);
	return 0;
    }
    if ((start + strlen(str)) > size)
    {
	(void)close(f);
	return 0;
    }

    if ((size = (int)lseek(f, (off_t)start, 0)) < 0)
    {
	(void)close(f);
	return 0;
    }

    size = write(f, str, strlen(str));

    (void)close(f);

    if (size <= 0) {
	return 0;
    }

    return 1;
}


int
file_size(char *file)
{
    struct stat st;

    file = check_valid_path(file, current_object, "file_size", 0);
    if (!file)
	return -1;

    if (file[0] == '/')
	file++;

    if (!legal_path(file))
	return -1;

#ifdef PURIFY
    (void)memset(&st, '\0', sizeof(st));
#endif

    if (stat(file, &st) == -1)
	return -1;
    if (S_IFDIR & st.st_mode)
	return -2;
    return (int)st.st_size;
}

int 
file_time(char *file)
{
    struct stat st;

    file = check_valid_path(file, current_object, "file_time", 0);

    if (!file)
	return 0;

#ifdef PURIFY
    (void)memset(&st, '\0', sizeof(st));
#endif

    if (stat(file, &st) == -1)
	return 0;
    return (int)st.st_mtime;
}

typedef struct {
    double avg1;
    double avg5;
    double avg15;
    double last_time;
} av_t;

static void
update_av(av_t *av, double amount)
{
    long double delta = current_time - av->last_time;
    av->last_time = current_time;
    
                                
    av->avg1 = expl(-delta / 60.0l) * av->avg1 +
	(1.0l - expl(-1.0l/60.0l)) * amount;

    av->avg5 = expl(-delta / 300.0l) * av->avg5 +
	(1.0l - expl(-1.0l/300.0l)) * amount;

    av->avg15 = expl(-delta / 900.0l) * av->avg15 +
	(1.0l - expl(-1.0l/900.0l)) * amount;
}

static av_t tcp_av = {0.0, 0.0, 0.0, 0.0};

void 
update_tcp_av(void) 
{
    update_av(&tcp_av, 1);
}

static av_t udp_av = {0.0, 0.0, 0.0, 0.0};

void 
update_udp_av(void) 
{
    update_av(&udp_av, 1);
}

static av_t alarm_av = {0.0, 0.0, 0.0, 0.0};

void 
update_alarm_av(void) 
{
    update_av(&alarm_av, 1);
}

static av_t load_av = {0.0, 0.0, 0.0, 0.0};

void 
update_load_av(void) 
{
    update_av(&load_av, 1);
}

static av_t runq_av = {0.0, 0.0, 0.0, 0.0};

void 
update_runq_av(double l)
{
    update_av(&runq_av, l);
}

static av_t compile_av = {0.0, 0.0, 0.0, 0.0};

void 
update_compile_av(int lines)
{
    update_av(&compile_av, lines);
}

char *
query_load_av(void)
{
    static char buff[1024];
    update_av(&load_av, 0);
    update_av(&compile_av, 0);
    update_av(&alarm_av, 0);
    update_av(&udp_av, 0);
    update_av(&tcp_av, 0);
    snprintf(buff, sizeof(buff) - 1,
	     "%.2f/%.2f/%.2f cmds/s, "
	     "%.2f/%.2f/%.2f comp lines/s, "
	     "%.2f/%.2f/%.2f alarms/s, "
	     "%.2f/%.2f/%.2f udp requests/s, "
	     "%.2f/%.2f/%.2f service requests/s, "
	     "%.2f/%.2f/%.2f runqueue length (current %ld)",
	     load_av.avg1, load_av.avg5, load_av.avg15,
	     compile_av.avg1, compile_av.avg5, compile_av.avg15,
	     alarm_av.avg1, alarm_av.avg5, alarm_av.avg15,
	     udp_av.avg1, udp_av.avg5, udp_av.avg15,
	     tcp_av.avg1, tcp_av.avg5, tcp_av.avg15,
	     runq_av.avg1, runq_av.avg5, runq_av.avg15, num_tasks + 1);
    buff[sizeof(buff) - 1] = 0;
    return buff;
}
