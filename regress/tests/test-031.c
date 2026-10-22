/* Native parser and formatter security boundaries. */
#pragma strict_types

void
create()
{
    string name, nested, fmt;
    mixed value;
    int i;

    foreach (int length : ({ 1022, 1023, 1024, 8192 }))
    {
        name = sprintf("%" + length + "s", "x");
        if (str2val("$1@" + name + "$") != 0)
            throw("Unexpected restored object\n");
    }
    if (str2val(val2str(this_object())) != this_object())
        throw("Object round trip failed\n");

    nested = " ";
    for (i = 0; i < 22; ++i)
        nested += nested;
    if (str2val("1" + nested) != 1)
        throw("Allowed input size rejected\n");
    nested += nested;
    if (str2val("1" + nested) != 0)
        throw("Excessive input size accepted\n");

    nested = "0,";
    for (i = 0; i < 16; ++i)
        nested += nested;
    if (!pointerp(str2val("({" + nested + "})")))
        throw("Allowed aggregate rejected\n");
    nested += nested;
    if (str2val("({" + nested + "})") != 0)
        throw("Excessive aggregate accepted\n");

    nested = "0";
    for (i = 0; i < 63; ++i)
        nested = "({" + nested + ",})";
    if (!pointerp(str2val(nested)))
        throw("Allowed nesting rejected\n");
    if (str2val("({" + nested + ",})") != 0)
        throw("Excessive nesting accepted\n");
    nested = "0";
    for (i = 0; i < 64; ++i)
        nested = "([0:" + nested + ",])";
    if (str2val(nested) != 0)
        throw("Excessive mapping nesting accepted\n");
    foreach (string malformed : ({ "({", "([", "({0", "([0", "([0:0", "\"abc\\", "$1@x", "$999999999999999999999@x$" }))
        if (str2val(malformed) != 0)
            throw("Malformed value accepted\n");
    value = str2val("({1,([2:3,]),\"text\",})");
    if (value[0] != 1 || value[1][2] != 3 || value[2] != "text")
        throw("Container round trip failed\n");

    foreach (string conversion : ({ "e", "f", "g" }))
        foreach (string sign : ({ "", "+", " " }))
        {
            fmt = "%" + sign;
            if (!strlen(sprintf(fmt + ".6" + conversion, 1.25)))
                throw("Normal float formatting failed\n");
            foreach (int precision : ({ 99, 100, 1000 }))
                if (!catch(sprintf(fmt + "." + precision + conversion, 1.25)))
                    throw("Excessive float precision accepted\n");
            if (!catch(sprintf(fmt + ".*" + conversion, -1, 1.25)) ||
                !catch(sprintf(fmt + ".*" + conversion, 2147483648, 1.25)))
                throw("Invalid star precision accepted\n");
        }
    if (strlen(sprintf("%.80f", 1.25)) != 82 ||
        !catch(sprintf("%f", 1.0e100)))
        throw("Float output bound failed\n");
    if (!catch(sprintf("%.999999999999999999999f", 1.25)) ||
        !catch(sprintf("%999999999999999999999s", "x")))
        throw("Overflowing decimal format accepted\n");

    foreach (string path : ({ "", "/", ".", "x", ".c" }))
    {
        restore_object(path);
        restore_map(path);
    }
}
