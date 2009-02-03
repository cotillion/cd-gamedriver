#ifndef _lint_h_
#define _lint_h_

#include <sys/types.h>
#include <setjmp.h>
#include <stdlib.h>
#include <unistd.h>

#include "master.h"

#if (defined(malloc) || defined(calloc))
/* Linux lossage */
#undef malloc
#undef calloc
#endif

/*
 * Some structure forward declarations are needed.
 */
#define DEBUG_CHK_REF	1
#define DEBUG_RESET	2
#define DEBUG_CLEAN_UP	4
#define DEBUG_SWAP	8
#define DEBUG_OUTPUT	16
#define DEBUG_CONNECT	32
#define DEBUG_TELNET	64
#define DEBUG_RESTORE	128
#define DEBUG_OB_REF	256
#define DEBUG_PROG_REF	512
#define DEBUG_LOAD	1024
#define DEBUG_DESTRUCT	2048
#define DEBUG_LIVING	4096
#define DEBUG_COMMAND	8192
#define DEBUG_ADD_ACTION 16384
#define DEBUG_SENTENCE	32768
#define DEBUG_BREAK_POINT 65536

/* Calculation of average */

struct program;
struct function;
struct svalue;
struct sockaddr;
struct reloc;
struct object;
struct mapping;
struct vector;
struct closure;

#ifndef PROFILE
#define INLINE inline
#else
#define INLINE
#endif

extern double current_time;
extern double alarm_time;

int write_file (char *, char *);
int file_size (char *);
int file_time (char *);
void remove_all_players (void);
void *xalloc (unsigned int);
void *tmpalloc (size_t);
long long new_call_out (struct closure *, double, double);
void add_action (struct closure *, struct svalue *, int);

void enable_commands (int);
void register_program(struct program*);
int tail (char *);
struct object *get_interactive_object (int);
void enter_object_hash (struct object *);
void remove_object_hash (struct object *);
struct object *lookup_object_hash (char *);
void add_otable_status (char *);
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void clear_otable(void);
#endif
void free_vector (struct vector *);
char *query_load_av (void);
void update_compile_av (int);
void update_runq_av (double);
struct vector *map_array (struct vector *arr, struct closure *);
struct vector *make_unique (struct vector *arr, struct closure *fun, struct svalue *skipnum);

struct vector *filter_arr (struct vector *arr, struct closure *);
int match_string (char *, char *);
struct object *get_empty_object (void);
void assign_svalue (struct svalue *, struct svalue *);
void assign_svalue_no_free (struct svalue *to, struct svalue *from);
void add_string_status (char *);
void notify_no_command (void);
void clear_notify (void);
void throw_error (void);
int call_var(int, struct closure *);
char *show_closure(struct closure *f);
char *getclosurename(struct closure *);
struct closure *alloc_closure(int);
int legal_closure(struct closure *);
void set_living_name (struct object *,char *);
void remove_living_name (struct object *);
struct object *find_living_object (char *);
int lookup_predef (char *);
void yyerror (char *);
char *dump_trace (int);
void resolve_master_fkntab(void);
void load_parse_information (void);
void free_parse_information (void);
int parse_command (char *, struct object *);
struct svalue *apply (char *, struct object *, int, int);
INLINE void push_string (char *, int);
INLINE void push_mstring (char *);
void push_number (long long);
void push_object (struct object *);
struct object *clone_object (char *);
void init_num_args (void);
int restore_object (struct object *, char *);
int m_restore_object (struct object *, struct mapping *);
struct mapping *m_save_object(struct object *);
struct vector *slice_array (struct vector *, long long, long long);
int query_idle (struct object *);
char *implode_string (struct vector *, char *);
struct object *query_snoop (struct object *);
struct vector *all_inventory (struct object *);
struct vector *deep_inventory (struct object *, int);
struct object *environment (struct svalue *);
struct vector *add_array (struct vector *, struct vector *);
char *get_f_name (int);
void startshutdowngame (int);
void set_notify_fail_message (char *, int);
struct vector *users (void);
void destruct_object (struct object *);
int set_snoop (struct object *, struct object *);
void ed_start (char *, struct closure *);
int command_for_object (char *, struct object *);
int remove_file (char *);
int input_to (struct closure *, int);
int parse (char *, struct svalue *, char *, struct svalue *, int);
struct object *object_present (struct svalue *, struct svalue *);
void call_function (struct object *, int , unsigned int, int);
void store_line_number_info (int, int);
int find_status (struct program *, char *, int);
void free_prog (struct program *);
char *stat_living_objects (void);
#ifdef OPCPROF
void opcdump (void);
#endif
struct vector *allocate_array (long long);
void init_machine (void);
void reset_machine (void);
void clear_state (void);
void preload_objects (int);
long long random_number (long long, int, char *);
int replace_interactive (struct object *ob, struct object *obf, char *);
void set_current_time (void);
char *time_string (time_t);
char *process_string (char *, int);
#ifdef DEBUG
void update_ref_counts_for_players (void);
void count_ref_from_call_outs (void);
void check_a_lot_ref_counts (struct program *);
#endif
char *read_file (char *file, int, int);
char *read_bytes (char *file, int, size_t);
int write_bytes (char *file, int, char *str);
char *check_valid_path (char *, struct object *, char *, int);
char *get_srccode_position_if_any (void);
void logon (struct object *ob);
struct svalue *apply_master_ob (int, int num_arg);
struct vector *explode_string (char *str, char *del);
char *string_copy (char *);
void remove_object_from_stack (struct object *ob);
void compile_file (void);
char *function_exists (char *, struct object *);
void set_inc_list (struct svalue *sv);
int legal_path (char *path);
struct vector *get_dir (char *path);
void get_simul_efun (void);
extern struct object *simul_efun_ob;
struct vector *match_regexp (struct vector *v, char *pattern);

extern char *get_srccode_position(int, struct program *);
void *malloc(size_t);
void free(void *);
void *realloc(void *, size_t);

double current_cpu(void);
struct vector *multiply_array(struct vector *vec, long long factor);
char *multiply_string(char *str, long long factor);

#ifdef PROFILE_LPC
long double get_profile_timebase(void);
void set_profile_timebase(long double timebase);
void update_prog_profile(struct program *prog, double now, double delta, double tot_delta);
void update_func_profile(struct function *funp, double now, double delta, double tot_delta, int calls);
#endif
extern struct svalue *sp;
extern struct object *previous_ob;
extern struct object *obj_list;
extern struct object *current_interactive;
void update_alarm_av(void);

#endif

