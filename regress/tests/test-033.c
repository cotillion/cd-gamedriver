/* Explicit master denials apply in both legacy and strict modes. */
#pragma strict_types

void
create()
{
    if (users() != 0 || query_ip_number(this_object()) != 0 ||
        query_ip_name(this_object()) != 0 || query_remote_port(this_object()) != 0 ||
        query_ip_ident(this_object()) != 0)
        throw("Privacy denial permitted access\n");
    set_this_player(this_object());
    if (exec(this_object(), this_object()) != 0 ||
        snoop(this_object(), this_object()) != 0)
        throw("Session denial permitted access\n");
    set_this_player(0);
    if (!catch(crypt("password", "xx")))
        throw("Crypt denial permitted access\n");
    move_object(find_object("/secure/master"));
    if (environment(this_object()))
        throw("Movement denial permitted access\n");
}
