#pragma strict_types

void
create()
{
    int i, j, k;

    for (i = 0; i < 100000; i++)
	for (j = 0; j < 100000; j++)
	    for (k = 0; k < 100000; k++)
		;
}
