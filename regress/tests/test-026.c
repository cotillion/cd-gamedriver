#pragma strict_types

inherit "/tests/base.c.c";

void
create()
{
    if (crypt("foo", "ab") != "abQ9KY.KfrYrc")
        throw("DES crypt is broken");        

    if (crypt("foo", "$1$BxY4Q1.U$") != "$1$BxY4Q1.U$CuizrialdJ9aeX30y/xJt/")
        throw("MD5 crypt is broken");

    string pass = "foofoo!";
    string c = crypt(pass, "$1$");
    if (crypt(pass, c) != c)
        throw("MD5 crypt is broken");
}
