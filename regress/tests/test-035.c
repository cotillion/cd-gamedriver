/* Both stacks must recover after overflow and remain usable for later calls. */
#pragma strict_types

int
recurse(int depth)
{
    if (depth)
        return recurse(depth - 1);
    return 1;
}

void
create()
{
    if (recurse(100) != 1)
        throw("Normal recursion failed\n");
    if (!catch(recurse(2048)))
        throw("Control stack overflow was not detected\n");
    if (recurse(100) != 1)
        throw("Control stack did not recover after overflow\n");
    int caught = 0;
    try {
        recurse(2048);
    } catch (mixed err) {
        caught = 1;
        if (recurse(100) != 1)
            throw("Control stack did not recover inside handler\n");
    }
    if (!caught)
        throw("Control stack overflow was not detected in try\n");
    if (recurse(100) != 1)
        throw("Control stack did not recover after try\n");
}
