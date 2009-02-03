#pragma no_clone
#pragma no_inherit
#pragma strict_types

nomask void
create()
{
    set_auth(this_object(), ({ -1, 0 }) );
}

nomask object
ob_pointer()
{
    return this_object();
}
