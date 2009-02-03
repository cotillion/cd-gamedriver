#pragma strict_types

void
test()
{
    catch(this_object()->nonexistant());

    // It crashes as we return from this function
}

void
create()
{
    this_object()->test();
    test();			// Only this call crashes
}
