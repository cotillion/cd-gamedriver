#pragma strict_types

void
create()
{
    int a, b;

    a = 1 ?: 2;
    b = 0 ? 1 : 2;
    if (a != 1)
	throw("Bad result from :? operation\n");
    if (b != 2)
	throw("Bad result from :? operation\n");
}
