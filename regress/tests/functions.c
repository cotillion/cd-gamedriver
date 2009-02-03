#pragma strict_types

string
func(mixed a)
{
    return "test";
}

void
create()
{
    function g1, g2, g3, g4, g5, g6, g7;
    object ob;

    ob = this_object();

    g1 = &ob->func();
    g2 = &ob->func(&func());
    g3 = mkfunction("func",ob);
    g4 = &func();
    g5 = &func(&func());
    g6 = &file_name(this_object());
    g7 = &sin(10.0);

    previous_object()->set_funcs(g1, g2, g3, g4, g5, g6, g7);
}

void
remove_object()
{
    destruct();
}
