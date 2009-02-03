void update_tcp_av(void);
void update_udp_av(void);
void update_alarm_av(void);
void update_load_av(void);
char *tmpstring_copy(char *);
extern int eval_cost;
void mainloop(void);
void backend(void);
void init_tasks(void);

struct task {
    void (*fkn)(void *);
    void *arg;
    struct task *next, *prev;
};

struct task *create_task(void (*f)(void *), void *arg);
void reschedule_task(struct task *);
void remove_task(struct task*);

