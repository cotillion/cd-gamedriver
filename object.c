#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <memory.h>

#include "config.h"
#include "lint.h"
#include "mstring.h"
#include "interpret.h"
#include "object.h"
#include "comm.h"
#include "super_snoop.h"
#include "sent.h"
#include "exec.h"
#include "mapping.h"
#include "mudstat.h"
#include "hash.h"
#include "main.h"
#include "simulate.h"
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
#include "call_out.h"
#endif

#include "inline_svalue.h"

extern int d_flag, s_flag;
extern int total_num_prog_blocks, total_prog_block_size;
extern char *add_slash(char *);
int tot_alloc_dest_object = 0;
int tot_removed_object = 0;

#define align(x) ( ((x) + (sizeof(void*)-1) )  &  ~(sizeof(void*)-1) )

struct object *previous_ob;

int tot_alloc_object, tot_alloc_object_size;

struct savebuf {
    unsigned int max_size;
    unsigned int size;
    char *buf;
    FILE *f;
};


#define STRBUFSIZE 4096
#define MAX_DEPTH  10000

void
add_strbuf(struct savebuf *sbuf, char *str)
{
    size_t len = strlen(str);

    if (sbuf->f) {
	fwrite(str, len, 1, sbuf->f);
	return;
    }
    
    if (sbuf->size + len + 1 > sbuf->max_size)
    {
	char *nbuf;
	size_t nsize;
	nsize = ((len + sbuf->size) / STRBUFSIZE + 1) * STRBUFSIZE;
	nbuf = xalloc(nsize);
	(void)memcpy(nbuf, sbuf->buf, sbuf->size);
	sbuf->max_size = nsize;
	free(sbuf->buf);
	sbuf->buf = nbuf;
    }
    (void)strcpy(&(sbuf->buf[sbuf->size]), str);
    sbuf->size += len;
}

#define Fprintf(s) if (fprintf s == EOF) failed=1

static void save_one (struct savebuf *,struct svalue *);

static void
save_string(struct savebuf *f, char *s)
{
    static char buf[2];

    buf[1] = '\0';
    
    add_strbuf(f, "\"");
    while (*s)
    {
	switch (*s)
	{
	case '"':
	    add_strbuf(f, "\\\"");
	    break;
	case '\\':
	    add_strbuf(f, "\\\\");
	    break;
	case '\n':
	    add_strbuf(f, "\\n");
	    break;
	default:
	    buf[0] = *s;
	    add_strbuf(f, buf);
	    break;
	}
	s++;
    }
    add_strbuf(f, "\"");
    
}

/*
 * Encode an array of elements.
 */
static void 
save_array(struct savebuf *f, struct vector *v)
{
    int i;

    add_strbuf(f, "({");
    for (i = 0; i < v->size; i++)
    {
	save_one(f, &v->item[i]);
	add_strbuf(f,",");
    }
    add_strbuf(f,"})");
}

static void 
save_mapping(struct savebuf *f, struct mapping *m)
{
    int i;
    struct apair *p;

    add_strbuf(f, "([");
    for (i = 0; i < m->size; i++)
    {
	for(p = m->pairs[i]; p; p = p->next)
	{
	    save_one(f, &p->arg);
	    add_strbuf(f,":");
	    save_one(f, &p->val);
	    add_strbuf(f,",");
	}
    }
    add_strbuf(f,"])");
}

static void
save_one(struct savebuf *f, struct svalue *v)
{
    static char buf[48];
    static int depth = 0;

    if (++depth > MAX_DEPTH)
    {
        free(f->buf);
        depth = 0;
        error("Too deep recursion\n");
    }

    switch(v->type) {
    case T_FLOAT:
	(void)sprintf(buf,"#%a#", v->u.real);
	add_strbuf(f, buf);
	break;
    case T_NUMBER:
	(void)sprintf(buf, "%lld", v->u.number);
	add_strbuf(f, buf);
	break;
    case T_STRING:
	save_string(f, v->u.string);
	break;
    case T_POINTER:
	save_array(f, v->u.vec);
	break;
    case T_MAPPING:
	save_mapping(f, v->u.map);
	break;
    case T_OBJECT:
	(void)sprintf(buf, "$%d@", v->u.ob->created);
	add_strbuf(f, buf);
	add_strbuf(f, v->u.ob->name);
	add_strbuf(f, "$");
        break;
#if 0
    case T_FUNCTION:
	add_strbuf(f, "&FUNCTION&"); /* XXX function */
	break;
#endif
    default:
	add_strbuf(f, "0");
	break;
    }

    depth--;
}

/*
 * Save an object to a file.
 * The routine checks with the function "valid_write()" in /obj/master.c
 * to assertain that the write is legal.
 */
void save_object(struct object *ob, char *file)
{
    char *name, *tmp_name;
    size_t len;
    int i, j;
    FILE *f;
    int failed = 0;
    struct savebuf sbuf;
    /* struct svalue *v; */

    if (ob->flags & O_DESTRUCTED)
	return;

    file = check_valid_path(file, ob, "save_object", 1);
    if (file == 0)
	error("Illegal use of save_object()\n");

    len = strlen(file);
    name = alloca(len + 2 + 1);
    (void)strcpy(name, file);
    (void)strcat(name, ".o");
    /*
     * Write the save-files to different directories, just in case
     * they are on different file systems.
     */
    tmp_name = alloca(len + 2 + 4 + 1);
    (void)sprintf(tmp_name, "%s.tmp", name);
    f = fopen(tmp_name, "w");
    if (s_flag)
	num_filewrite++;
    if (f == 0) {
	error("Could not open %s for a save.\n", tmp_name);
    }
    failed = 0;
    
    sbuf.size = 0;
    sbuf.max_size = 80;
    sbuf.buf = xalloc(80);
    sbuf.f = f;
    
    for (j = 0; j < (int)ob->prog->num_inherited; j++)
    {
	struct program *prog = ob->prog->inherit[j].prog;
	if (ob->prog->inherit[j].type & TYPE_MOD_SECOND ||
	    prog->num_variables == 0)
	    continue;
	for (i = 0; i < (int)prog->num_variables; i++) {
	    struct svalue *v =
		&ob->variables[i + ob->prog->inherit[j].variable_index_offset];
	    
	    if (ob->prog->inherit[j].prog->variable_names[i].type & TYPE_MOD_STATIC)
		continue;
	    if (v->type == T_NUMBER || v->type == T_STRING || v->type == T_POINTER
		|| v->type == T_MAPPING || v->type == T_OBJECT || v->type == T_FLOAT) {	/* XXX function */
		add_strbuf(&sbuf, ob->prog->inherit[j].prog->variable_names[i].name);
		add_strbuf(&sbuf, " ");
		save_one(&sbuf, v);
		add_strbuf(&sbuf, "\n");
	    }
	}
    }

    fwrite(sbuf.buf, sbuf.size, 1, f);
    free(sbuf.buf);
    if (fclose(f) == EOF)
	failed = 1;
    if (failed)
    {
	(void)unlink(tmp_name);
	error("Failed to save to file. Disk could be full.\n");
    }
    if (rename(tmp_name, name) == -1)
    {
	(void)unlink(tmp_name);
	perror(name);
	(void)printf("Failed to link %s to %s\n", tmp_name, name);
	error("Failed to save object !\n");
    }
}

/*
 * Save an object to a mapping.
 */
struct mapping *
m_save_object(struct object *ob)
{
    int i, j;
    struct mapping *ret;
    struct svalue s = const0;
    
    if (ob->flags & O_DESTRUCTED)
	return allocate_map(0);	/* XXX is this right /LA */

    ret = allocate_map((short)(ob->prog->num_variables +
			       ob->prog->inherit[ob->prog->num_inherited - 1].
			       variable_index_offset));
    
    for (j = 0; j < (int)ob->prog->num_inherited; j++)
    {
	struct program *prog = ob->prog->inherit[j].prog;
	if (ob->prog->inherit[j].type & TYPE_MOD_SECOND ||
	    prog->num_variables == 0)
	    continue;
	for (i = 0; i < (int)prog->num_variables; i++)
	{
	    struct svalue *v =
		&ob->variables[i + ob->prog->inherit[j].
			       variable_index_offset];
	    
	    if (prog->variable_names[i].type & TYPE_MOD_STATIC)
		continue;
	    free_svalue(&s);
	    s.type = T_STRING;
	    s.string_type = STRING_MSTRING;
	    s.u.string = make_mstring(prog->variable_names[i].name);
	    assign_svalue(get_map_lvalue(ret, &s, 1), v);
	}
    }
    
    free_svalue(&s);
    return ret;
}

void 
save_map(struct object *ob, struct mapping *map, char *file)
{
    char *name, *tmp_name;
    struct apair *i;
    size_t len;
    int j;
    FILE *f;
    struct savebuf sbuf;
    int failed = 0;
    /* struct svalue *v; */


    file = check_valid_path(file, ob, "save_map", 1);
    if (file == 0)
	error("Illegal use of save_map()\n");

    for (j = 0; j < map->size; j++)
    {
	for (i = map->pairs[j]; i; i = i->next) {
	    if (i->arg.type != T_STRING)
		error("Non-string index in mapping\n");
	    if (strpbrk(i->arg.u.string, " \n\r\t\f\v\b") != NULL)
		error("Mapping index cannot have whitespace\n");
	}
    }

    len = strlen(file);
    name = alloca(len + 2 + 1);
    (void)strcpy(name, file);
    (void)strcat(name, ".o");
    /*
     * Write the save-files to different directories, just in case
     * they are on different file systems.
     */
    tmp_name = alloca(len + 2 + 4 + 1);
    (void)sprintf(tmp_name, "%s.tmp", name);
    f = fopen(tmp_name, "w");
    if (s_flag)
	num_filewrite++;
    if (f == 0) {
	error("Could not open %s for a save.\n", tmp_name);
    }
    failed = 0;
    
    sbuf.size = 0;
    sbuf.max_size = 80;
    sbuf.buf = xalloc(80);
    sbuf.f = f;
    
    for (j = 0; j < map->size; j++)
    {
	for (i = map->pairs[j]; i; i = i->next) {
	    struct svalue *v =
		&i->val;
	    
	    if (i->arg.type != T_STRING)
		continue;
	    if (v->type == T_NUMBER || v->type == T_STRING || v->type == T_POINTER
		|| v->type == T_MAPPING || v->type == T_OBJECT || v->type == T_FLOAT) { /* XXX function */
		add_strbuf(&sbuf, i->arg.u.string);
		add_strbuf(&sbuf, " ");
		save_one(&sbuf, v);
		add_strbuf(&sbuf, "\n");
	    }
	}
    }
    fwrite(sbuf.buf, sbuf.size, 1, f);
    free(sbuf.buf);
    if (fclose(f) == EOF)
	failed = 1;
    if (failed)
    {
	(void)unlink(tmp_name);
	error("Failed to save to file. Disk could be full.\n");
    }
    if (rename(tmp_name, name) == -1)
    {
	(void)unlink(tmp_name);
	perror(name);
	(void)printf("Failed to link %s to %s\n", tmp_name, name);
	error("Failed to save mapping !\n");
    }
}

char *
valtostr(struct svalue *sval)
{
    struct savebuf sbuf;
   
    sbuf.buf = xalloc(80);
    sbuf.size = 0;
    sbuf.max_size = 80;
    sbuf.buf[0] = 0;
    sbuf.f = NULL;
    
    save_one(&sbuf, sval);

    return sbuf.buf;
}
#define BIG 1000

int restore_one (struct svalue *, char **);

static struct vector *
restore_array(char **str)
{
    struct svalue *tmp;
    int nmax = BIG;
    int i, k;

    tmp = (struct svalue *)alloca(nmax * sizeof(struct svalue));
    for(k = 0; k < nmax; k++)
	tmp[k] = const0;
    i = 0;
    for(;;)
    {
	if (**str == '}')
	{
	    if (*++*str == ')')
	    {
		struct vector *v;

		++*str;
		v = allocate_array(i);
		(void)memcpy((char *)&v->item[0], (char *)tmp, sizeof(struct svalue) * i);
		return v;
	    }
	    else
		break;
	} 
	else
	{
	    if (i >= nmax)
	    {
		struct svalue *ntmp;

		ntmp = (struct svalue *)alloca(nmax * 2 * sizeof(struct svalue));
		(void)memcpy((char *)ntmp, (char *)tmp, sizeof(struct svalue) * nmax);
		tmp = ntmp;
		nmax *= 2;
		for(k = i; k < nmax; k++)
		    tmp[k] = const0;
	    }
	    if (!restore_one(&tmp[i++], str))
		break;
	    if (*(*str)++ != ',')
		break;
	}
    }
    for (i--; i >= 0; i--)
	free_svalue(&(tmp[i]));
    return 0;
}

static struct mapping *
restore_mapping(char **str)
{
    struct mapping *m;

    m = allocate_map(0);
    for(;;)
    {
	if (**str == ']')
	{
	    if (*++*str == ')')
	    {
		++*str;
		return m;
	    }
	    else
		break;
	}
	else
	{
	    struct svalue arg, *val;
	    arg = const0;
	    if (!restore_one(&arg, str))
		break;
      	    if (*(*str)++ != ':')
	    {
		free_svalue(&arg);
		break;
	    }
	    val = get_map_lvalue(m, &arg, 1);
	    free_svalue(&arg);

	    if (!restore_one(val, str) ||
		*(*str)++ != ',')
		break;
	}
    }
    free_mapping(m);
    return 0;
}

/* XXX function */
int
restore_one(struct svalue *v, char **msp)
{
    char *q, *p, *s;

    s = *msp;
    switch(*s) {
    case '(':
	switch(*++s)
	{
	case '[':
	    {
		struct mapping *map;
		s++;
		map = restore_mapping(&s);
		if (!map) {
		    return 0;
		}
		free_svalue(v);
		v->type = T_MAPPING;
		v->u.map = map;
	    }
	    break;
	    
	case '{':
	    {
		struct vector *vec;
		s++;
		vec = restore_array(&s);
		if (!vec) {
		    return 0;
		}
		free_svalue(v);
		v->type = T_POINTER;
		v->u.vec = vec;
	    }
	    break;
	    
	default:
	    return 0;
	}
	break;
    case '"':
	for(p = s+1, q = s; *p && *p != '"'; p++) 
	{
	    if (*p == '\\') {
		switch (*++p) {
		case 'n':
		    *q++ = '\n';
		    break;
		default:
		    *q++ = *p;
		    break;
		}
	    } else {
		/* Have to be able to restore old format... */
		if (*p == '\r')
		    *q++ = '\n';
		else
		    *q++ = *p;
	    }
	}
	*q = 0;
	if (*p != '"')
            return 0;
	free_svalue(v);
	v->type = T_STRING;
	v->string_type = STRING_MSTRING;
	v->u.string = make_mstring(s);
	s = p+1;
	break;
    case '$':
        {
	    int ct;
            struct object *ob;
            char name[1024];
            char *b = strchr(s + 1,'$');

	    *name = '\0';
            if (b == NULL)
                return 0;
            if (sscanf(s,"$%d@%[^$ \n\t]$",&ct,name) != 2)
                return 0;
            ob = find_object2(name);
	    free_svalue(v);
            if (ob && ob->created == ct)
            {
                v->type = T_OBJECT;
                v->u.ob = ob;
                add_ref(ob,"restore_one");
            }
            else
            {
                v->type = T_NUMBER;
                v->u.number = 0;
            }
            s = b + 1;
        }
        break;
    case '#':
	{
	    double f;
	    char *b = strchr(s + 1, '#');
	    if (b == NULL)
		return 0;
            f = strtod(s + 1, NULL);
	    free_svalue(v);
	    v->type = T_FLOAT;
	    v->u.real = f;
	    s = b + 1;
	}
	break;
    default:
	if (!isdigit(*s) && *s != '-')
	    return 0;
	free_svalue(v);
	v->type = T_NUMBER;
	v->u.number = atoll(s);
	while(isdigit(*s) || *s == '-')
	    s++;
	break;
    }
    *msp = s;
    return 1;
}

int 
restore_object(struct object *ob, char *file)
{
    char *name, var[100], *buff, *space;
    size_t len;
    FILE *f;
    struct object *save = current_object;
    struct stat st;
    int p;

    if (current_object != ob)
	fatal("Bad argument to restore_object()\n");
    if (ob->flags & O_DESTRUCTED)
	return 0;

    file = check_valid_path(file, ob, "restore_object", 0);
    if (file == 0)
	error("Illegal use of restore_object()\n");

    len = strlen(file);
    name = alloca(len + 3);
    (void)strcpy(name, file);
    if (name[len-2] == '.' && name[len-1] == 'c')
	name[len-1] = 'o';
    else
	(void)strcat(name, ".o");
    f = fopen(name, "r");
    if (s_flag)
	num_fileread++;
    if (!f || fstat(fileno(f), &st) == -1) {
	if (f) 
	    (void)fclose(f);
	return 0;
    }
    if (st.st_size == 0) {
	(void)fclose(f);
	return 0;
    }
    buff = xalloc((size_t)st.st_size + 1);
    current_object = ob;
    
    for (;;) {
	struct svalue *v;

	if (fgets(buff, (int)st.st_size + 1, f) == 0)
	    break;
	/* Remember that we have a newline at end of buff ! */
	space = strchr(buff, ' ');
	if (space == 0 || space - buff >= sizeof (var)) {
	    (void)fclose(f);
	    free(buff);
	    error("Illegal format when restoring %s.\n", file);
	}
	(void)strncpy(var, buff, (size_t)(space - buff));
	var[space - buff] = '\0';
	p = find_status(ob->prog, var, TYPE_MOD_STATIC);
	if (p == -1)
	    continue;
	v = &ob->variables[p];
	space++;
	if (!restore_one(v, &space)) {
	    (void)fclose(f);
	    free(buff);
	    error("Illegal format when restoring %s from %s.\n", var, file);
	}
    }
    current_object = save;
    if (d_flag & DEBUG_RESTORE)
	debug_message("Object %s restored from %s.\n", ob->name, file);
    free(buff);
    (void)fclose(f);
    return 1;
}
int 
m_restore_object(struct object *ob, struct mapping *map)
{
    int p;
    int i;
    struct apair *j;

    if (ob->flags & O_DESTRUCTED)
	return 0;
    
    for (i = 0; i < map->size; i++)
    {
	for (j = map->pairs[i]; j ; j = j->next)
	{
	    if (j->arg.type != T_STRING)
		continue;
	    
	    if ((p = find_status(ob->prog, j->arg.u.string, TYPE_MOD_STATIC))
		== -1)
		continue;

	    assign_svalue(&ob->variables[p], &j->val);
	}
    }
    
    return 1;
}

void 
restore_map(struct object *ob, struct mapping *map, char *file)
{
    char *name, *buff, *space;
    size_t len;
    FILE *f;
    struct object *save = current_object;
    struct stat st;

    if (current_object != ob)
	fatal("Bad argument to restore_map()\n");
    if (ob->flags & O_DESTRUCTED)
	return;

    file = check_valid_path(file, ob, "restore_map", 0);
    if (file == 0)
	error("Illegal use of restore_map()\n");

    len = strlen(file);
    name = alloca(len + 2 + 1);
    (void)strcpy(name, file);
    if (name[len-2] == '.' && name[len-1] == 'c')
	name[len-1] = 'o';
    else
	(void)strcat(name, ".o");
    f = fopen(name, "r");
    if (s_flag)
	num_fileread++;
    if (!f || fstat(fileno(f), &st) == -1) {
	if (f) 
	    (void)fclose(f);
	return;
    }
    if (st.st_size == 0) {
	(void)fclose(f);
	return;
    }
    buff = xalloc((size_t)st.st_size + 1);
    
    for (;;) {
	struct svalue v;

	v.type = T_STRING;
	v.string_type = STRING_MSTRING;

	if (fgets(buff, (int)st.st_size + 1, f) == 0)
	    break;
	/* Remember that we have a newline at end of buff ! */
	space = strchr(buff, ' ');
	if (space == 0) {
	    (void)fclose(f);
	    free(buff);
	    error("Illegal format when restoring %s.\n", file);
	}
	*space++ = '\0';
	v.u.string = make_mstring(buff);
	
	if (!restore_one(get_map_lvalue(map,&v,1), &space)) {
	    (void)fclose(f);
	    free(buff);
	    free_svalue(&v);
	    error("Illegal format when restoring %s.\n", file);
	}
	free_svalue(&v);
    }
    current_object = save;
    free(buff);
    (void)fclose(f);
}

void 
free_object(struct object *ob, char *from)
{
    if (d_flag & DEBUG_OB_REF)
	(void)printf("Subtr ref to ob %s: %d (%s)\n", ob->name,
		      ob->ref, from);
    if (!ob->ref || --ob->ref > 0)
	return;
    if (!(ob->flags & O_DESTRUCTED))
    {
	/* This is fatal, and should never happen. */
	fatal("FATAL: Object %p %s ref count 0, but not destructed (from %s).\n",
	    ob, ob->name, from);
    }
    if (ob->interactive)
	fatal("Tried to free an interactive object.\n");
    /*
     * If the program is freed, then we can also free the variable
     * declarations.
     */
    if (ob->name) {
	if (lookup_object_hash(ob->name) == ob)
	    fatal("Freeing object %s but name still in name table\n", ob->name);
	free(ob->name);
	ob->name = 0;
    }
    tot_alloc_object--;
    tot_alloc_dest_object--;
    tot_removed_object++;
    free((char *)ob);
    tot_alloc_object_size -= sizeof (struct object);
}

void 
add_ref(struct object *ob, char *from)
{
    INCREF(ob->ref);
    if (d_flag & DEBUG_OB_REF)
	(void)printf("Add reference to object %s: %d (%s)\n", ob->name,
	       ob->ref, from);
}

/*
 * Allocate an empty object, and set all variables to 0. Note that a
 * 'struct object' already has space for one variable. So, if no variables
 * are needed, we allocate a space that is smaller than 'struct object'. This
 * unused (last) part must of course (and will not) be referenced.
 */
struct object *
get_empty_object()
{
    static struct object NULL_object;
    struct object *ob;
    int size = sizeof (struct object);

    tot_alloc_object++;
    tot_alloc_object_size += size;
    ob = (struct object *)xalloc(sizeof(struct object));
    /* marion
     * Don't initialize via memset, this is incorrect. E.g. the bull machines
     * have a (char *)0 which is not zero. We have structure assignment, so
     * use it.
     */
    *ob = NULL_object;
    ob->ref = 1;
    return ob;
}

#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
static void
force_remove_object(struct object *ob)
{
    remove_object_from_stack(ob);
    remove_object_hash(ob);
    remove_task(ob->callout_task);
    delete_all_calls(ob);
    ob->next_all = obj_list_destruct;
    ob->prev_all = NULL;
    obj_list_destruct = ob;
    ob->flags |= O_DESTRUCTED;
    tot_alloc_dest_object++;
}

void
remove_all_objects()
{
    extern struct object *master_ob, *vbfc_object, *auto_ob, *simul_efun_ob;
    struct gdexception exception_frame;
    struct object *ob;
    int n;

    exception_frame.e_exception = NULL;
    exception_frame.e_catch = 0;

    for (;;) {
	ob = obj_list;
	n = 0;
	while (ob == master_ob ||
	       ob == vbfc_object ||
	       ob == auto_ob ||
	       ob == simul_efun_ob) {
	    ob = ob->next_all;
	    if (n++ > 3)
		break;
	}
	if (n > 3 || ob == 0)
	    break;
	if (setjmp(exception_frame.e_context))
	    clear_state();
	else {
	    exception = &exception_frame;
	    ob->prog->flags &= ~PRAGMA_RESIDENT;
	    destruct_object(ob);
	    exception = NULL;
	}
    }
    /*
     * In case we have objects referenced through
     * closure cache
     */
    clear_closure_cache();
    /*
     * Remove VBFC object
     */
    force_remove_object(vbfc_object);
    free_object(vbfc_object, "remove_all_objects");
    vbfc_object = NULL;
    /*
     * Remove master object
     */
    force_remove_object(master_ob);
    free_object(master_ob, "remove_all_objects");
    master_ob = NULL;
    /*
     * Remove simul_efun object
     */
    force_remove_object(simul_efun_ob);
    free_object(simul_efun_ob, "remove_all_objects");
    simul_efun_ob = NULL;
    /*
     * Remove auto object
     */
    force_remove_object(auto_ob);
    free_object(auto_ob, "remove_all_objects");
    auto_ob = NULL;
}
#endif

#if 0
/*
 * For debugging purposes.
 */
void 
check_ob_ref(struct object *ob, char *from)
{
    struct object *o;
    int i;

    for (o = obj_list, i=0; o; o = o->next_all) {
	if (o->inherit == ob)
	    i++;
    }
    if (i+1 > ob->ref) {
	fatal("FATAL too many references to inherited object %s (%d) from %s.\n",
	      ob->name, ob->ref, from);
	if (current_object)
	    (void)fprintf(stderr, "current_object: %s\n", current_object->name);
	for (o = obj_list; o; o = o->next_all) {
	    if (o->inherit != ob)
		continue;
	    (void)fprintf(stderr, "  %s\n", ob->name);
	}
    }
}
#endif /* 0 */

static struct object *hashed_living[LIVING_HASH_SIZE];

static int num_living_names, num_searches = 1, search_length = 1;

#if BITNUM(LIVING_HASH_SIZE) == 1
/* This one only works for even power-of-2 table size, but is faster */
#define LivHash(s) (hashstr16((s), 100) & ((LIVING_HASH_SIZE)-1))
#else
#define LivHash(s) (hashstr((s), 100, LIVING_HASH_SIZE))
#endif


struct object *
find_living_object(char *str)
{
    struct object **obp, *tmp;
    struct object **hl;

    num_searches++;
    hl = &hashed_living[LivHash(str)];
    for (obp = hl; *obp; obp = &(*obp)->next_hashed_living) {
	search_length++;
	if (!((*obp)->flags & O_ENABLE_COMMANDS))
	    continue;
	if (strcmp((*obp)->living_name, str) == 0)
	    break;
    }
    if (*obp == 0)
	return 0;
    /* Move the found ob first. */
    if (obp == hl)
	return *obp;
    tmp = *obp;
    *obp = tmp->next_hashed_living;
    tmp->next_hashed_living = *hl;
    *hl = tmp;
    return tmp;
}

struct vector *
find_living_objects(char *str)
{
    struct object **obp;
    struct object **hl;
    struct vector *ret;
    int count;

    num_searches++;
    hl = &hashed_living[LivHash(str)];
    for (count = 0, obp = hl; *obp; obp = &(*obp)->next_hashed_living) {
	search_length++;
	if (!((*obp)->flags & O_ENABLE_COMMANDS))
	    continue;
	if (strcmp((*obp)->living_name, str) == 0)
	    count++;
    }

    ret = allocate_array(count);

    for (count = 0, obp = hl; *obp; obp = &(*obp)->next_hashed_living)
    {
	search_length++;
	if (!((*obp)->flags & O_ENABLE_COMMANDS))
	    continue;
	if (strcmp((*obp)->living_name, str) == 0)
	{
	    ret->item[count].type = T_OBJECT;
	    ret->item[count++].u.ob = *obp;
	    add_ref(*obp, "find_living_objects");
	}
    }

    return ret;
}

void 
set_living_name(struct object *ob, char *str)
{
    struct object **hl;

    if (ob->flags & O_DESTRUCTED)
	return;
    if (ob->living_name) {
	remove_living_name(ob);
#ifdef SUPER_SNOOP
	if (ob->interactive && ob->interactive->snoop_fd >= 0) {
	    (void) close(ob->interactive->snoop_fd);
	    ob->interactive->snoop_fd = -1;
	}
#endif
    }
    if (!*str)
	return;
    num_living_names++;
    hl = &hashed_living[LivHash(str)];
    ob->next_hashed_living = *hl;
    *hl = ob;
    ob->living_name = make_sstring(str);
#ifdef SUPER_SNOOP
    check_supersnoop(ob);
#endif
    return;
}

void 
remove_living_name(struct object *ob)
{
    struct object **hl;

    num_living_names--;
    if (!ob->living_name)
	fatal("remove_living_name: no living name set.\n");
    hl = &hashed_living[LivHash(ob->living_name)];
    while(*hl) {
	if (*hl == ob)
	    break;
	hl = &(*hl)->next_hashed_living;
    }
    if (*hl == 0)
	fatal("remove_living_name: Object named %s no in hash list.\n",
	      ob->living_name);
    *hl = ob->next_hashed_living;
    free_sstring(ob->living_name);
    ob->next_hashed_living = 0;
    ob->living_name = 0;
}

char *
stat_living_objects()
{
    static char tmp[400];

    (void)sprintf(tmp,"Hash table of living objects:\n-----------------------------\n%d living named objects, average search length: %4.2f\n",
	    num_living_names, (double)search_length / num_searches);
    return tmp;
}

void 
reference_prog (struct program *progp, char *from)
{
    INCREF(progp->ref);
    if (d_flag & DEBUG_PROG_REF)
	(void)printf("reference_prog: %s ref %d (%s)\n",
	    progp->name, progp->ref, from);
}

struct program *prog_list;

/* Add a program to the list of all programs */
void register_program(struct program *prog)
{
    if (prog_list)
    {
	prog->next_all = prog_list;
	prog->prev_all = prog_list->prev_all;
	prog_list->prev_all->next_all = prog;
	prog_list->prev_all = prog;
	prog_list = prog;
    }
    else
	prog_list = prog->next_all = prog->prev_all = prog;
}

/*
 * Decrement reference count for a program. If it is 0, then free the prgram.
 * The flag free_sub_strings tells if the propgram plus all used strings
 * should be freed. They normally are, except when objects are swapped,
 * as we want to be able to read the program in again from the swap area.
 * That means that strings are not swapped.
 */
void 
free_prog(struct program *progp)
{
    extern int total_program_size;
    int i;
    
    if (d_flag & DEBUG_PROG_REF)
	(void)printf("free_prog: %s\n", progp->name);
    if (!progp->ref || --progp->ref > 0)
	return;
    if (progp->ref < 0)
	fatal("Negative ref count for prog ref.\n");

    if (progp == prog_list)
	prog_list = progp->next_all;
    
    progp->prev_all->next_all = progp->next_all;
    progp->next_all->prev_all = progp->prev_all;
    if (progp->next_all == progp)
	prog_list = 0;
    
    total_program_size -= progp->exec_size;
    
    total_prog_block_size -= progp->total_size;
    total_num_prog_blocks--;
    
    /* Free all function names. */
    for (i=0; i < (int)progp->num_functions; i++)
	if (progp->functions[i].name)
	    free_sstring(progp->functions[i].name);

    /* Free all variable names */
    for (i=0; i < (int)progp->num_variables; i++)
	free_sstring(progp->variable_names[i].name);

    /* Free all inherited objects */
    for (i=0; i < (int)progp->num_inherited - 1; i++)
    {
	free_prog(progp->inherit[i].prog);
	free_sstring(progp->inherit[i].name);
    }
    free_sstring(progp->inherit[i].name);
    free(progp->name);
    free((char *)progp->program);
    if (progp->line_numbers != 0)
	free((char *)progp->line_numbers);
    free((char *)progp);
}

void 
create_object(struct object *ob)
{
    int i;

    if (!(ob->flags & O_CREATED))
    {
	ob->flags |= O_CREATED;
	ob->created = current_time;
	for (i = 0; i < (int)ob->prog->num_inherited; i++)
	    if (!(ob->prog->inherit[i].type & TYPE_MOD_SECOND))
	    {
		if (ob->prog->inherit[i].prog->ctor_index !=
		    (unsigned short) -1)
		{
		    call_function(ob, i,
				  (unsigned int)ob->prog->inherit[i].prog->ctor_index, 0);
		    pop_stack();
		}
	    }
	if (search_for_function("create", ob->prog))
	{
	    call_function(ob, function_inherit_found,
			  (unsigned int)function_index_found, 0);
	    pop_stack();
	}
    }
}
void 
recreate_object(struct object *ob, struct object *old_ob)
{
    int i;
    int need_create = 0;

    if (!(ob->flags & O_CREATED))
    {
        need_create = 1;
	ob->flags |= O_CREATED;
	if (!ob->created)
	    ob->created = current_time;
	for (i = 0; i < (int)ob->prog->num_inherited; i++)
	    if (!(ob->prog->inherit[i].type & TYPE_MOD_SECOND))
	    {
		if (ob->prog->inherit[i].prog->ctor_index !=
		    (unsigned short) -1)
		{
		    call_function(ob, i,
				  (unsigned int)ob->prog->inherit[i].prog->ctor_index, 0);
		    pop_stack();
		}
	    }
    }
    if (search_for_function("recreate", ob->prog))
    {
	push_object(old_ob);
	call_function(ob, function_inherit_found,
		      (unsigned int)function_index_found, 1);
	pop_stack();
    }
    else if (need_create && search_for_function("create", ob->prog))
    {
        call_function(ob, function_inherit_found,
		      (unsigned int)function_index_found, 0);
	pop_stack();
    }
}

/*
 * Returns a list of all inherited files.
 *
 */
struct vector *
inherit_list(struct object *ob)
{
    struct vector *ret;
    int inh;

    ret = allocate_array(ob->prog->num_inherited);

    for (inh = 0; inh < (int)ob->prog->num_inherited; inh++ )
    {
	ret->item[inh].type = T_STRING;
	ret->item[inh].string_type = STRING_MSTRING;
	ret->item[inh].u.string = add_slash(ob->prog->inherit[inh].prog->name);
    }
    return ret;
}

void
change_ref(struct object *to, struct object *from, char *msg)
{
    if (from)
        add_ref(from, msg);
    if (to)
        free_object(to, msg);
}

void
warnobsolete(struct object *ob, char *msg)
{
    extern int warnobsoleteflag;

    ob->flags |= O_OBSOLETE_WARNING;
    if (!warnobsoleteflag)
	return;
    (void)fprintf(stderr, "OBSOLETE: %s: %s\n", ob->name, msg);
}
