struct object;

void update_actions(struct object *aob);
char *get_gamedriver_info(char *str);
void fatal(char *fmt, ...) __attribute__((format(printf, 1, 2))) __attribute__ ((noreturn));
void warning(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void error(char *fmt, ...) __attribute__((format(printf, 1, 2))) __attribute__ ((noreturn));
