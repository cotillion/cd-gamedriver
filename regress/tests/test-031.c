/* Native formatter security boundaries. */
#pragma strict_types

void
create()
{
    string fmt;

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

}
