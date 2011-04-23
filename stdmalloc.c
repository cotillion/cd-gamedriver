#include <malloc.h>

/*
 * Basic information for the 'malloc' debug call
 */ 
char *
dump_malloc_data()
{
    static char buf[512];    
    struct mallinfo info = mallinfo();

    snprintf(buf, sizeof(buf),
        "%-17s %13s %13s %13s\n"
        "%-17s %13d %13d %13d\n"
        "%-17s %13d %13d %13d\n"
        "Total heap size: %d",
        "",            "small", "ordinary", "total",
        "allocated blocks", info.smblks, info.ordblks, info.smblks + info.ordblks,
        "used memory", info.usmblks, info.uordblks, info.usmblks + info.uordblks,
        info.arena);
    return &buf[0];
}
    

