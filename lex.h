#define MLEN 8192
#define NSIZE 256

struct lpc_predef_s {
    char *flag;
    struct lpc_predef_s *next;
};

extern struct lpc_predef_s *lpc_predefs;

void add_pre_define(char *define);    
void start_new_file(FILE *f);
int handle_include(char *include_name, int ignore_errors);
void end_new_file(void);
void free_inc_list(void);
