#pragma strict_types

inherit "/tests/base.c.c";

void
create()
{
    mixed a = ({ 1, 2, 3, 4 });

    a[3] = a;

    val2str(a);

    write("OK\n");
}
