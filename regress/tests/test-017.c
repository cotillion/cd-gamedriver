#pragma strict_types

function g1, g2, g3, g4, g5, g6, g7;

string
func(mixed a)
{
    return "test";
}

void
create()
{
    object ob;
    function f1, f2, f3, f4, f5;

    ob = clone_object("/tests/functions");

    f1 = &ob->func();
    f2 = &ob->func(f1);
    f3 = mkfunction("func",ob);
    f4 = &func() @ &ob->func();
    f5 = &ob->func() @ &func();

    if (!functionp(f1))
	throw("parent functionp(&ob->func()) broken\n");
    if (!functionp(f2))
	throw("parent functionp(&ob->func(f1)) broken\n");
    if (!functionp(f3))
	throw("parent functionp(mkfunction(\"func\", ob)) broken\n");
    if (!functionp(f4))
	throw("parent functionp(&func @ &ob->func()) broken\n");
    if (!functionp(f5))
	throw("parent functionp(&ob->func() @ &func()) broken\n");
    if (!functionp(g1))
	throw("child functionp(&ob->func()) broken\n");
    if (!functionp(g2))
	throw("child functionp(&ob->func(&func())) broken\n");
    if (!functionp(g3))
	throw("child functionp(mkfunction(\"func\", ob)) broken\n");
    if (!functionp(g4))
	throw("child functionp(&func()) broken\n");
    if (!functionp(g5))
	throw("child functionp(&func(&func())) broken\n");
    if (!functionp(g6))
	throw("child functionp(&file_name(this_object())) broken\n");
    if (!functionp(g7))
	throw("child functionp(&sin(10.0)) broken\n");

    if (g6() != "/tests/functions#1")
	throw("child &file_name(this_object()) evaluates wrong\n");

    ob->remove_object();

    if (functionp(f1))
	throw("parent functionp(&ob->func()) did not detect destructed object\n");
    if (functionp(f2))
	throw("parent functionp(&ob->func(f1)) did not detect destructed object\n");
    if (functionp(f3))
	throw("parent functionp(mkfunction(\"func\", ob)) did not detect destructed object\n");
    if (functionp(f4))
	throw("parent functionp(&func() @ &ob->func()) did not detect destructed object\n");
    if (functionp(f5))
	throw("parent functionp(&ob->func() @ &func()) did not detect destructed object\n");
    if (functionp(g1))
	throw("child functionp(&ob->func()) did not detect destructed object\n");
    if (functionp(g2))
	throw("child functionp(&ob->func(&func())) did not detect destructed object\n");
    if (functionp(g3))
	throw("child functionp(mkfunction(\"func\", ob)) did not detect destructed object\n");
    if (functionp(g4))
	throw("child functionp(&func()) did not detect destructed object\n");
    if (functionp(g5))
	throw("child functionp(&func(&func())) did not detect destructed object\n");
    if (!functionp(g6))
	throw("child functionp(&file_name(this_object())) was affected by destructed object\n");
    if (!functionp(g7))
	throw("child functionp(&sin(10.0)) was affected by destructed object\n");
}

void 
set_funcs(function f1, function f2, function f3, function f4,
	  function f5, function f6, function f7)
{
    g1 = f1; 
    g2 = f2;
    g3 = f3;
    g4 = f4;
    g5 = f5;
    g6 = f6;
    g7 = f7;
}
