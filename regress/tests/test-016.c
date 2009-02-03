#pragma strict_types

void
create()
{
    function funp;

    funp = this_object;
    if (!functionp(funp))
	throw("Bad functionp()\n");
}
