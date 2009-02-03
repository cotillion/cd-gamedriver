/*
 Function: for_each
 Params  : mixed elements      (the array/mapping of elements to use)
           function mapfunc    (the function to recieve the elements)
 By      : Asmodean
*/

void
for_each(mixed elements, function mapfunc)
{
    int i;

    arr=elements;
    if (mappingp(elements))
	elements = m_values(elements);
    for (i=0;i<sizeof(arr);i++)
	mapfunc(elements[i]);
}
