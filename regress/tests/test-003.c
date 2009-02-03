void
create()
{
   object ob;

   parse_command("test", ({ ({ this_object() }) }), "%o", ob);
}

string
id()
{
    return "test";
}
