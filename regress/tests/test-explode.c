#pragma strict_types

void
create()
{
    string *parts = explode("test", "foo");
    if (sizeof(parts) != 1)
        throw("Explode is broken in no match.\n");

    parts = explode("test", "");
    if (sizeof(parts) != 4)
        throw("Explode is broken in letter test.\n");

    // This will fail for kingdoms explode
    parts = explode(" test ", "");
    if (sizeof(parts) != 6)
        throw("Explode is broken in space test.\n");
}
