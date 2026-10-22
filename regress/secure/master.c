#pragma strict_types
#pragma no_clone
#pragma no_inherit

public nomask void
write_console(string str)
{
    write_socket(str);
}

static object
get_vbfc_object()
{
    return "/secure/vbfc_object"->ob_pointer();
}

public nomask string *
define_include_dirs()
{
    return ({ "/include" });
}

static void
flag(string str)
{
    write("Unknown flag " + str + "\n");
}

static string
fix_name(string file)
{
    string name;

    if (sscanf(file, "%s.c", name) != 1)
        return "/tests/ " +file;
    return "/tests/" + name;
}

static string *
start_boot(int empty)
{
    string *tests;

    tests = get_dir("/tests/test-*.c");
    tests = map(tests, fix_name);
    sort_array(tests);
    return tests;
}

static void
preload_boot(string file)
{
    string err;
    object ob;

    if (file_size(file + ".c") == -1)
        return;

    write("Loading " + file + ".c -> ");
    if (err = catch(call_other(file, "???")))
        write(err);
    else
        write("Ok.\n");
    if (ob = find_object(file))
        debug("destroy", ob);
}

static void
final_boot()
{
    debug("shutdown");
}

/* The regression mudlib explicitly authorizes its fixture file access. */
static int
valid_read(string path, object caller, string operation)
{
    return 1;
}

static int
valid_write(string path, object caller, string operation)
{
    return 1;
}

static varargs int
valid_debug(object caller, string operation, mixed arg)
{
    return 1;
}

static int
valid_crypt(object caller, mixed salt)
{
    return file_name(caller) == "/tests/test-026";
}

/* Explicit denials keep the fixture independent of legacy missing-hook rules. */
static int valid_users(object caller) { return 0; }
static int valid_move(object caller, object destination) { return 0; }
static int valid_resident(object caller) { return 0; }
static int valid_exec(string program, object to, object from) { return 0; }
static int valid_snoop(object caller, object snooper, object snoopee) { return 0; }
static int valid_set_ip_number(object caller, object target, string ip) { return 0; }
static int valid_query_ip_ident(object caller, object target) { return 0; }
static varargs int valid_query_ip_number_name(mixed name, object caller,
                                            object target) { return 0; }
