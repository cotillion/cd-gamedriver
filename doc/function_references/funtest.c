/*
** Allow the race name to be changed permanently.
** It has to be done though an autoloaded object.
*/
inherit "/std/object";

void
create_object()
{
    set_name("funtest");
    set_pname("funtest");
    set_short("funtest");
    set_pshort("funtest");
    set_long("test object, don't use it\n");
}

int 
afun(int i, int j)
{
    write("called afun "); write(i); write("\n");
    return 123+i+10*j;
}

int square(int x) { return x*x; }

int cube(int x) { return x*x*x; }

void baz()
{
    function *funarr;
    function c_or_s;

    funarr = ({ square, cube });
    c_or_s = funarr[1];
    write(c_or_s(3));
}

static mixed 
compose_util(function f, function g, mixed x)
{
    return f(g(x));
}

function
compose(function f, function g)
{
    return &compose_util{f,g};
}

function globf;

void larm(string s1, string s2, string s3)
{
    write("larm "+s1+s2+s3+"\n");
}

int
funtest(string str)
{
    function f, g;
    int i;
    string s;
    object ob;
    function h;

    i = (int)globf(15);
    write(i); write("\n");

    h = compose(square, cube);
    write(h(2)); write("\n");

    f = this_player()->query_name;
    write(f() + "\n");
    f = ctime;
    s = (string)f(100000);
    write(s+"\n");
    f = atoi;
    i = (int)f("1234");
    write(i); write("\n");
    f = afun;
    g = &f{10};
    i = (int)f(1,1000);
    write("testit: "+str+" f(...)=");write(i);write("\n");
    i = (int)g(10000);
    write("testit: "+str+" g(...)=");write(i);write("\n");
    f = &g{100000};
    i = (int)f();
    write("testit: "+str+" f(...)=");write(i);write("\n");
    f(88);
    if (functionp(afun)) write("ok1\n");
    if (functionp(f)) write("ok2\n");
    if (functionp(i)) write("bad1\n");
    f = square;
    write(f(5)); write("\n");
    baz(); write("\n");
    h = compose(square, cube);
    write(h(2)); write("\n");

    i = set_alarm(2.0, 0.0, &larm{"arg1","arg2"}, "arg3");
    s = sprintf("Larm id=%d\n", i);
    write(s);
    return 1;
}

int
atest(string str)
{
    int i;
    int *a, *b;
    a = ({ 1, 2 });
    b = ({ 3, 4 });
    i = 0;
    i = b[a[i]];
    write(i); write("\n");
    return 1;
}


static function globg;

int
level()
{
    write(sprintf("level=%d\n", globg()));
    return 1;
}

void
itest1(string inp, string s)
{
    write(s+inp+"\n");
}

int
itest()
{
    write("say something");
    input_to(&itest1{"yes"});
    return 1;
}

int
done(string s)
{
    write(s+" done\n");
}


int
doed()
{
    ed("foo", &done{"it's"});
    return 1;
}

void
init()
{
    globf = square;
    globg = this_player()->query_wiz_level;
    add_action(funtest, "funtest", 0);
    add_action(level, "level", 0);
    add_action(atest, "atest", 0);
    add_action(itest, "itest", 0);
    add_action(doed, "doed", 0);
}

