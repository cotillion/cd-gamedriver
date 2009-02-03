#pragma strict_types

void
create()
{
    object ob;
    object *l;

    call_other("/tests/xyzzy", "???");
    ob = find_object("/tests/xyzzy");
    l = find_living("test", 1);
    l = 0;
    debug("destruct", ob);
}
