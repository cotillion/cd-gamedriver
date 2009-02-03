/*
 * These are some statistics variables that are used if we run -S mode
 */

extern int	num_move,		/* Number of move_object() */
    		num_mcall,		/* Number of calls to master ob */
		num_fileread,		/* Number of file reads */
		num_filewrite,		/* Number of file writes */
		num_compile;		/* Number of compiles */

#define MUDSTAT_FILE 		"MUDstatistics"
#define MUDSTAT_LOGTIME 	50
#define MUDSTAT_LOGEVAL 	25000

/*
 * Prototypes
 */
void reset_mudstatus(void);
void mudstatus_set(int active, int eval_lim, int time_lim);
void print_mudstatus(char *, int, int, int);
int get_millitime(void);
int get_processtime(void);
