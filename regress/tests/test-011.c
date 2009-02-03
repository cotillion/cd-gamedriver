#pragma strict_types

static void
nothing()
{
}

void
create()
{
    int i;

    for (i = 0; i < 1024 ; i++)
	if (set_alarm(120.0, 0.0, nothing) == 0)
	    break;
    if (i == 1024)
	throw("Driver compiled without per-object alarm limit\n");
}
