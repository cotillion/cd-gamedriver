/*
 * Definition of an object.
 * If the object is inherited, then it must not be destructed !
 *
 * The reset is used as follows:
 * 0: There is an error in the reset() in this object. Never call it again.
 * 1: Normal state.
 * 2 or higher: This is an interactive player, that has not given any commands
 *		for a number of reset periods.
 */
#include "config.h"

#include "interpret.h"

#define O_CREATED		0x02  /* Has 'create()' been called */
#define O_ENABLE_COMMANDS	0x04  /* Can it execute commands ? */
#define O_CLONE			0x08  /* Is it cloned from a master copy ? */
#define O_DESTRUCTED		0x10  /* Is it destructed ? */
#define O_SWAPPED		0x20  /* Is it swapped to file */
#define O_ONCE_INTERACTIVE	0x40  /* Has it ever been interactive ? */
#define O_OBSOLETE_WARNING	0x80  /* Object has already generated a warning */
#define O_ADDED_COMMAND 	0x100 /* Has it created a sentence? */

struct call;

struct object {
    unsigned short flags;	/* Bits or'ed together from above */
    unsigned short debug_flags;
    int created;		/* Time of creation of this object */
    int time_of_ref;		/* Time when last referenced. Used by swap */
    unsigned int ref;		/* Reference count. */
#ifdef DEBUG
    int extra_ref;		/* Used to check ref count. */
#endif
    struct program *prog;
    char *name;
    struct task *callout_task;
    struct call *call_outs;
    short num_callouts;
    struct object *next_call_out;
    struct object *next_all, *prev_all, *next_inv, *next_hash;
    struct object *contains;
    struct object *super;		/* Which object surround us ? */
    struct object *shadowing;		/* Is this object shadowing ? */
    struct object *shadowed;		/* Is this object shadowed ? */
    struct interactive *interactive;	/* Data about an interactive player */
    struct sentence *sent;
    struct object *next_hashed_living;
    char *living_name;			/* Name of living object if in hash */
    struct svalue *variables;		/* All variables to this program */
    struct svalue auth;                 /* The protected auth variable */
};

#define WARNOBSOLETE(ob, msg)	if (((ob)->flags & O_OBSOLETE_WARNING) == 0) \
				    warnobsolete(ob, msg)

void warnobsolete(struct object *ob, char *msg);

struct object *load_object (char *, int, struct object *, int);
struct object *find_object (char *);
struct object *get_empty_object(void);
struct object *find_object (char *);
struct object  *find_object2 (char *);

extern struct object *current_object;
extern struct object *command_giver;
extern struct object *obj_list;
extern struct object *obj_list_destruct;

struct value;
void remove_destructed_objects(void);
void save_object (struct object *, char *);
void move_object (struct object *);
void add_ref (struct object *, char *);
void change_ref (struct object *, struct object *, char *);
void free_object (struct object *, char *);
void reference_prog (struct program *, char *);
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void remove_all_objects(void);
#endif

struct mapping;
int restore_object (struct object *, char *);
void save_map(struct object *, struct mapping *, char *);
int restore_one(struct svalue *, char **);
void restore_map(struct object *, struct mapping *, char *);
void create_object(struct object *);
void recreate_object(struct object *, struct object *);
