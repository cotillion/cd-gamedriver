#pragma no_clone
#pragma no_inherit
#pragma strict_types

static nomask mixed
sort(mixed arr, function lfunc) = "sort";

varargs nomask mixed
sort_array(mixed arr, mixed lfunc, object obj)
{
    if (stringp(lfunc)) {
	if (!obj)
	    obj = previous_object();
	lfunc = mkfunction(lfunc, obj);
    }
    return sort(arr, lfunc);
}
