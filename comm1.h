void write_socket(char *, struct object *);
void write_gmcp(struct object *, char *);
void add_message(char *, ...) __attribute__((format(printf, 1, 2)));
void add_message2(struct object *, char *, ...) __attribute__((format(printf, 2, 3)));
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void clear_ip_table(void);
#endif
extern int num_player;
