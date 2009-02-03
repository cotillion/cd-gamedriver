#pragma strict_types

void
create()
{
    if (min(1, 2) != 1)
	throw("min() does not work!\n");
    if (min("a", "b") != "a")
	throw("min() does not work!\n");
    if (min(1.0, 2.0) > 1.1)
	throw("min() does not work!\n");
    if (max(1, 2) != 2)
	throw("max() does not work!\n");
    if (max("a", "b") != "b")
	throw("max() does not work!\n");
    if (max(1.0, 2.0) < 1.9)
	throw("max() does not work!\n");
}
