#pragma strict_types

#define ESC 27

/*
 * Some tests around the #define of ANSI_COLOR
 */

void
test_strlen()
{
    /*
     * Map string to a pair of lengths.
     * First is expected length with ANSI_COLOR defined,
     * second is without.
     */
    mapping strings = ([
        "just a string\n": ({ 14, 14 }),
        sprintf(
            // The 'm' in 'normalizer' closes the sequence
            "%c[31mRed Tacos%c[0Forgot to close the normalizer sequence.\n",
            ESC, ESC): ({ 26, 58 }),
        sprintf(
            "%c[31mRed Tacos%c[0m %c[38;5;208mOrange Tacos%c[0m\n",
            ESC, ESC, ESC, ESC): ({ 23, 47 }),
        0: ({ 0, 0 }),
        "": ({ 0, 0 })
    ]);

    foreach(string str, string *lengths: strings)
    {
        write("strlen() of [" + str + "] is " + strlen(str) + ". " +
              "When ANSI_COLOR is defined, we expect " + lengths[0] + ". " +
              "When not defined, we expect " + lengths[1] + ".\n");
    }
}

void
create()
{
    test_strlen();
}
