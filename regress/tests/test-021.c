#pragma strict_types

int
function_one(int arg)
{
    if (arg != 0x1234)
	throw("Bad argument!\n");
    return 0x5678;
}

static int
function_two(int arg)
{
    if (arg != 0x1234)
	throw("Bad argument!\n");
    return 0x5678;
}

private int
function_three(int arg)
{
    if (arg != 0x1234)
	throw("Bad argument!\n");
    return 0x5678;
}

void
create()
{
    if (call_self("function_one",   0x1234) != 0x5678)
	throw("Bad return value from call_self(\"function_one\", 0x1234)\n");
    if (call_self("function_two",   0x1234) != 0x5678)
	throw("Bad return value from call_self(\"function_two\", 0x1234)\n");
    if (call_self("function_three", 0x1234) != 0x5678)
	throw("Bad return value from call_self(\"function_three\", 0x1234)\n");
    if (call_selfv("function_one",   ({ 0x1234 }) ) != 0x5678)
	throw("Bad return value from call_selfv(\"function_one\", ({ 0x1234 }) )\n");
    if (call_selfv("function_two",   ({ 0x1234 }) ) != 0x5678)
	throw("Bad return value from call_selfv(\"function_two\", ({ 0x1234 }) )\n");
    if (call_selfv("function_three", ({ 0x1234 }) ) != 0x5678)
	throw("Bad return value from call_selfv(\"function_three\", ({ 0x1234 }) )\n");
}
