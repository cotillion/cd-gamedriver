#pragma strict_types

public int
crashme(int num, int a, int b, int c, int d, int e, int f, int g, int h, int i)
{
    if (num == 1)
	return crashme(1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
    return 0;
}

void
create()
{
    crashme(1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
}
