#pragma no_clone
#pragma no_inherit
#pragma strict_types

nomask varargs void dump_array(mixed a, string tab);
nomask varargs void dump_mapping(mapping m, string tab);
static void dump_elem(mixed sak, string tab);

nomask void
write(string str)
{
    "/secure/master"->write_console(str);
}

static string 
type_name(mixed etwas)
{
    if (intp(etwas))
	return "int";
    else if (floatp(etwas))
	return "float";
    else if (stringp(etwas))
	return "string";
    else if (objectp(etwas))
	return "object";
    else if (pointerp(etwas))
	return "array";
    else if (mappingp(etwas))
	return "mapping";
#ifdef _FUNCTION
    else if (functionp(etwas))
	return "function";
#endif
    return "!UNKNOWN!";
}

/*
 * Function name: dump_array
 * Description:   Dumps a variable with write() for debugging purposes.
 * Arguments:     a: Anything including an array
 */
nomask varargs void
dump_array(mixed a, string tab)
{
    int             n,
                    m;
    mixed 	    ix, val;

    if (!tab)
	tab = "";
    if (!pointerp(a) && !mappingp(a))
    {
	dump_elem(a, tab);
	return;
    }
    else if (pointerp(a))
    {
	write("(Array)\n");
	m = sizeof(a);
	n = 0;
	while (n < m)
	{
	    write(tab + "[" + n + "] =");
	    dump_elem(a[n], tab);
	    n += 1;
	}
    }
    else /* Mappingp */
	dump_mapping(a, tab);
}

/*
 * Function name: dump_mapping
 * Description:   Dumps a variable with write() for debugging purposes.
 * Arguments:     a: Anything including an array
 */
nomask varargs void
dump_mapping(mapping m, string tab)
{
    mixed *d;
    int i, s;
    string dval, val;

    if (!tab)
	tab = "";

    d = m_indexes(m);
    s = sizeof(d);
    write("(Mapping) ([\n");
    for(i = 0; i < s; i++) {
	if (intp(d[i]))
	    dval = "(int)" + d[i];

	if (floatp(d[i]))
	    dval = "(float)" + ftoa(d[i]);

	if (stringp(d[i]))
	    dval = "\"" + d[i] + "\"";

	if (objectp(d[i]))
	    dval = file_name(d[i]);

	if (pointerp(d[i]))
	    dval = "(array:" + sizeof(d[i]) + ")";

	if (mappingp(d[i]))
	    dval = "(mapping:" + m_sizeof(d[i]) + ")";
#ifdef _FUNCTION
	if (functionp(d[i]))
	    dval = sprintf("%O", d[i]);

	if (functionp(m[d[i]]))
	    val = sprintf("%O", m[d[i]]);
#endif
	
	if (intp(m[d[i]]))
	    val = "(int)" + m[d[i]];

	if (floatp(m[d[i]]))
	    val = "(float)" + ftoa(m[d[i]]);

	if (stringp(m[d[i]]))
	    val = "\"" + m[d[i]] + "\"";

	if (objectp(m[d[i]]))
	    val = file_name(m[d[i]]);

	if (pointerp(m[d[i]]))
	    val = "(array:" + sizeof(m[d[i]]) + ")";

	if (mappingp(m[d[i]]))
	    val = "(mapping:" + m_sizeof(m[d[i]]) + ")";

	write(tab + dval + ":" + val + "\n");

	if (pointerp(d[i]))
	    dump_array(d[i]);

	if (pointerp(m[d[i]]))
	    dump_array(m[d[i]]);

	if (mappingp(d[i]))
	    dump_mapping(d[i], tab + "   ");

	if (mappingp(m[d[i]]))
	    dump_mapping(m[d[i]], tab + "   ");
    }
    write("])\n");
}

static nomask void
dump_elem(mixed sak, string tab)
{
    if (pointerp(sak))
    {
	dump_array(sak, tab + "   ");
    }
    else if (mappingp(sak))
    {
	dump_mapping(sak, tab + "   ");
    }
    else
    {
	write("(" + type_name(sak) + ") ");
	if (objectp(sak))
	    write(file_name(sak));
	else if (floatp(sak))
	    write(ftoa(sak));
	else
	    write(sprintf("%O",sak));
    }
    write("\n");
}

/*
 * Function name: apply_array
 * Description:   apply a function to an array of arguments
 * Arguments:     f: the function, v: the array
 * Returns:       f(v1,...vn) if v=({v1,...vn})
 */
mixed
applyv(function f, mixed *v)
{
    function g = papplyv(f, v);
    return g();
}

/*
 * Function name: for_each
 * Description:   For each of the elements in the array 'elements', for_each
 *                calls func with it as as parameter.
 *                This is the same functionality as the efun map, but without
 *                the return value.
 * Arguments:     mixed elements      (the array/mapping of elements to use)
 *                function func       (the function to recieve the elements)
 */
void
for_each(mixed elements, function func)
{
    int i;
    int sz;
    mixed arr;

    arr = elements;
    if (mappingp(elements))
        arr = m_values(elements);
    sz = sizeof(arr);
    for(i = 0; i < sz; i++)
        func(arr[i]);
}

/*
 * Function name: constant
 * Description:   returns the first argument and ignores the second
 * Arguments:     x, y
 * Returns:       x
 */
mixed
constant(mixed x, mixed y)
{
    return x;
}

/*
 * Function name: identity
 * Description:   returns its argument unmodified
 * Arguments:     x
 * Returns:       x
 */
mixed
identity(mixed x)
{
    return x;
}


/*
 * Function name: not
 * Description:   returns the locigal inverse of the argument
 * Arguments:     x
 * Returns:       !x
 */
int 
not(mixed x)
{
    return !x;
}

int
mkcompare_util(function f, mixed x, mixed y)
{
    if (f(x,y))
	return -1;
    if (f(y,x))
	return 1;
    return 0;
}

/*
 * Function name: mkcompare
 * Description:   takes a normal comparison function, like < and
 *		  returns a function suitable for sort_array
 * Arguments:     f: the function
 * Returns:       another function
 */
function
mkcompare(function f)
{
    return &mkcompare_util(f);
}
