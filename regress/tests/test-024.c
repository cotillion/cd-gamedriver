#pragma strict_types

void
create()
{
    object *obs;

    parse_command("0", ({ this_object() }), "%i", obs);
    if (sizeof(obs))
	throw("parse_command botch\n");
}
