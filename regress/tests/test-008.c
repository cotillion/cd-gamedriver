#pragma strict_types

void
create()
{
    if ((sprintf("%-4s", "x") + "x") != "x   x")
	throw("sprintf botch\n");
}
