#include <malloc.h>

#include "memory.h"
/*
 * Basic information for the 'malloc' debug call
 */
char *
dump_malloc_data()
{
#if defined(__GLIBC__)
  char *bp;
  size_t size;

  FILE *stream = open_memstream (&bp, &size);
  malloc_info(0, stream);
  fclose (stream);
  return bp;
#else
    char *buf = xalloc(512);
    struct mallinfo info = mallinfo();

    snprintf(buf, 512, "%-17s %13s %13s %13s\n"
        "%-17s %13d %13d %13d\n"
        "%-17s %13d %13d %13d\n"
        "Total heap size: %d",
        "",            "small", "ordinary", "total",
        "allocated blocks", info.smblks, info.ordblks, info.smblks + info.ordblks,
        "used memory", info.usmblks, info.uordblks, info.usmblks + info.uordblks,
        info.arena);
    return buf;
 #endif
}


