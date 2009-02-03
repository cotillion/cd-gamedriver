#pragma strict_types

void
create()
{
    set_living_name("test");
    enable_commands();
    add_action(time, "time");
    commands();
}
