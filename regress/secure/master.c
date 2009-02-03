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
