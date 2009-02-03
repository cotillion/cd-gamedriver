struct sentence {
    char *verb;
    struct closure *funct;
    struct sentence *next;
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
    struct sentence *next_all, *prev_all;
#endif
    unsigned short short_verb;
};

#define	V_SHORT		0x1	/* Only leading characters count */
#define	V_NO_SPACE	0x2	/* A space is not required */

struct sentence *alloc_sentence(void);

void remove_sent (struct object *, struct object *),
    free_sentence (struct sentence *);
