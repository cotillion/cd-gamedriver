#pragma strict_types

void
create()
{
    mixed *a, *b;

    a = allocate(1);
    b = allocate(1);

    a[0] = a;
    b[0] = b;

    if (a == b)
	throw("Bad array handling\n");
}
