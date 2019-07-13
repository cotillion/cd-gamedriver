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
            "%c[31mRed Tacos%c[0Forgot to close the normalizer sequence.%c[0m\n",
            ESC, ESC, ESC): ({ 26, 62 }),
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
test_break_string()
{
    // 0: input
    // 1: expected output with ANSI_COLOR defined
    // 2: expected output without ANSI_COLOR defined
    mixed *strings = ({
        ({
            "Hello my Nice Friends. How are you today?\n",
            "Hello my\nNice\nFriends.\nHow are\nyou\ntoday?\n",
            "Hello my\nNice\nFriends.\nHow are\nyou\ntoday?\n"
        }),
        ({
            sprintf(
                "%c[31mRed Tacos%c[0m %c[38;5;208mOrange Tacos%c[0m\n",
                ESC, ESC, ESC, ESC),
            sprintf(
                "%c[31mRed Tacos%c[0m\n%c[38;5;208mOrange\nTacos%c[0m\n",
                ESC, ESC, ESC, ESC),
            sprintf(
                "%c[31mRed\nTacos%c[0m\n%c[38;5;208mOrange\nTacos%c[0m\n",
                ESC, ESC, ESC, ESC)
        }),
        ({
            0, 0, 0
        }),
        ({
            "", "", ""
        })
    });

    int i, sz;

    for(i = 0, sz = sizeof(strings); i < sz; i++)
    {
        string *triplet = strings[i];
        string str = "break_string() example " + (i + 1) +
            ": ";

        string broken = break_string(triplet[0], 10);

        if (broken == triplet[1])
        {
            str += "Equal to ANSI expected.";
        }
        else
        {
            str += "Not equal to ANSI expected.";
        }

        if (broken == triplet[2])
        {
            str += " Equal to non-ANSI expected.";
        }
        else
        {
            str += " Not equal to non-ANSI expected.";
        }

        write(str + "\n");
    }
}

/*
 * Interesting issue, in that we can't capture the output.
 * It's up to the user to verify the match.
 */
void
test_write_socket()
{
    object ob;

    call_other("/tests/xyzzy", "???");
    ob = find_object("/tests/xyzzy");

    set_this_player(ob);

    set_screen_width(10);

    // 0: input
    // 1: expected output with ANSI_COLOR defined
    // 2: expected output without ANSI_COLOR defined
    mixed *strings = ({
        ({
            "Hello my Nice Friends. How are you today?\n",
            "Hello my\nNice\nFriends.\nHow are\nyou\ntoday?\n",
            "Hello my\nNice\nFriends.\nHow are\nyou\ntoday?\n"
        }),
            ({
                sprintf(
                    "%c[31mRed Tacos%c[0m %c[38;5;208mOrange Tacos%c[0m\n",
                    ESC, ESC, ESC, ESC),
                sprintf(
                    "%c[31mRed Tacos%c[0m\n%c[38;5;208mOrange\nTacos%c[0m\n",
                    ESC, ESC, ESC, ESC),
                sprintf(
                    "%c[31mRed\nTacos%c[0m\n%c[38;5;208mOrange\nTacos%c[0m\n",
                    ESC, ESC, ESC, ESC)
            }),
            ({
                0, 0, 0
            }),
            ({
                "", "", ""
            })
    });

    int i, sz;

    for(i = 0, sz = sizeof(strings); i < sz; i++)
    {
        string *triplet = strings[i];

        write("output starts on next line:\n");
//        write_socket(triplet[0]);
        "/secure/master"->write_console(triplet[0]);
        write("end of output.\n");
        write("with ANSI we expect\n" + triplet[1] + "\n");
        write("without ANSI we expect\n" + triplet[2] + "\n");
    }
}

void
create()
{
    test_strlen();
    test_break_string();
    test_write_socket();
}
