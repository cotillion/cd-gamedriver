/* Nested commands must preserve the caller's verb on returns and errors. */
#pragma strict_types

void
check_verb(string verb, string trigger)
{
    if (query_verb() != verb || query_trigverb() != trigger)
        throw("Wrong command context: expected " + verb + "/" + trigger +
              ", got " + sprintf("%O/%O", query_verb(), query_trigverb()) + "\n");
}

int
inner_action()
{
    check_verb("inner", "in");
    return 1;
}

int
throwing_action()
{
    check_verb("boom", "boom");
    command("inner");
    check_verb("boom", "boom");
    throw("action failure");
    return 0;
}

int
rejecting_action()
{
    check_verb("fallback", "fall");
    command("inner");
    check_verb("fallback", "fall");
    return 0;
}

int
fallback_action()
{
    check_verb("fallback", "fa");
    return 1;
}

void
nested_helper()
{
    command("inner");
    check_verb("outer", "out");
}

int
return_from_try()
{
    try {
        command("inner");
        check_verb("outer", "out");
        return 1;
    } catch (mixed err) {
        throw("Unexpected exception on return from try\n");
    }
    return 0;
}

void
rethrow_helper()
{
    try {
        command("boom");
    } catch (mixed err) {
        check_verb("outer", "out");
        throw(err);
    }
    throw("Throwing command returned\n");
}

int
runtime_error_action()
{
    int zero = 0;
    check_verb("fault", "fault");
    return 1 / zero;
}

int
outer_action()
{
    check_verb("outer", "out");
    for (int i = 0; i < 10; ++i)
    {
        command("inner");
        check_verb("outer", "out");
    }
    nested_helper();
    check_verb("outer", "out");
    if (catch(command("inner")))
        throw("Unexpected exception in successful catch\n");
    check_verb("outer", "out");
    try {
        command("inner");
        check_verb("outer", "out");
    } catch (mixed err) {
        throw("Unexpected exception in successful try\n");
    }
    if (return_from_try() != 1)
        throw("Incorrect return from try\n");
    check_verb("outer", "out");
    if (catch(rethrow_helper()) != "action failure")
        throw("Rethrown exception changed\n");
    check_verb("outer", "out");
    if (!catch(command("fault")))
        throw("Expected native runtime error\n");
    check_verb("outer", "out");
    if (!catch(command("boom")))
        throw("Expected action failure\n");
    check_verb("outer", "out");
    try {
        command("boom");
    } catch (mixed err) {
        check_verb("outer", "out");
    }
    check_verb("outer", "out");
    command("no-such-action");
    check_verb("outer", "out");
    command("");
    check_verb("outer", "out");
    return 1;
}

void
create()
{
    enable_commands();
    add_action(inner_action, "in", 1);
    add_action(throwing_action, "boom");
    add_action(runtime_error_action, "fault");
    add_action(outer_action, "out", 1);
    add_action(fallback_action, "fa", 1);
    add_action(rejecting_action, "fall", 1);
    check_verb(0, 0);
    if (!command("outer"))
        throw("Outer command failed\n");
    check_verb(0, 0);
    if (!command("fallback"))
        throw("Fallback command failed\n");
    check_verb(0, 0);
    if (!catch(command("boom")))
        throw("Expected top-level action failure\n");
    check_verb(0, 0);
}
