#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "config.h"

#ifdef SUPER_SNOOP
#include "object.h"
#include "comm.h"
#include "super_snoop.h"

volatile int num_super_snooped;
extern struct interactive *all_players[MAX_PLAYERS];
static char super_snooped[256][16];

void 
read_snoop_file()
{
  FILE *f;
  int i;
  f = fopen("../snoops/snooped","r");
  if (f == NULL)
  {
      num_super_snooped = 0;
      return;
  }
  for (i = 0; fscanf(f, "%s", super_snooped[i]) != EOF && i < 256 ; i++)
  {
  }

  fclose(f);
  num_super_snooped = i;
}

void
check_supersnoop(struct object *ob)
{
    char snoop_file_path[32];
    if (!ob || !ob->interactive)
        return;

    if (ob->interactive->snoop_fd >= 0) {
        (void)close(ob->interactive->snoop_fd);
        ob->interactive->snoop_fd = -1;
    }
    if (!ob->living_name || !*ob->living_name)
        return;

    snprintf(snoop_file_path, sizeof(snoop_file_path), "../snoops/%s", ob->living_name);

#ifdef SUPER_SNOOP_ALL
    ob->interactive->snoop_fd = open(snoop_file_path, O_WRONLY | O_APPEND | O_CREAT, 0600);
#else
    for (int i = 0; i < num_super_snooped; i++) {
        if (strcmp(ob->living_name, super_snooped[i]) == 0) {
            ob->interactive->snoop_fd = open(snoop_file_path, O_WRONLY | O_APPEND | O_CREAT, 0600);
            break;
        }
    }
#endif
}

void 
update_snoop_file()
{
    read_snoop_file();
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (all_players[i] && all_players[i]->ob)
            check_supersnoop(all_players[i]->ob); 
    }
}

#endif
