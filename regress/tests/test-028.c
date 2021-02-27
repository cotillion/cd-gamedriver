#pragma strict_types

inherit "/tests/base.c";

int
try_return()
{
    try {
        return 1;
    } catch (mixed ex)  {
        return 2;
    }
}

void
create()
{
    if (try_return() != 1)
        throw("return inside try returns the wrong value\n");
}

