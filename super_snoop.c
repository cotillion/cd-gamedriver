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
static char super_snoopfile[256][32];

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
      (void)strcpy(super_snoopfile[i], "../snoops/");
      (void)strcat(super_snoopfile[i], super_snooped[i]);
  }

  fclose(f);
  num_super_snooped = i;
}

void 
update_snoop_file()
{
    int i, j;

    for (i = 0; num_super_snooped && i < MAX_PLAYERS; i++)
	if (all_players[i] && all_players[i]->snoop_fd >= 0) {
	    (void)close(all_players[i]->snoop_fd);
	    all_players[i]->snoop_fd = -1;
	    num_super_snooped--;
	}
    read_snoop_file();
    for (i = 0; i < MAX_PLAYERS; i++)
	for (j = 0; j < num_super_snooped; j++)
	    if (all_players[i] && all_players[i]->ob &&
		all_players[i]->ob->living_name &&
		strcmp(all_players[i]->ob->living_name, super_snooped[j]) == 0) {
		all_players[i]->snoop_fd = open(super_snoopfile[j],
						 O_WRONLY | O_APPEND | O_CREAT,
						 0600);
		break;
	    }
}

void
check_supersnoop(struct object *ob)
{
    int i;

    if (!ob || !ob->interactive)
	return;

    if (ob->interactive->snoop_fd >= 0) {
	(void)close(ob->interactive->snoop_fd);
	ob->interactive->snoop_fd = -1;
    }
    if (!ob->living_name || !*ob->living_name)
	return;

    for (i = 0; i < num_super_snooped; i++) {
	if (strcmp(ob->living_name, super_snooped[i]) == 0) {
	    ob->interactive->snoop_fd = open(super_snoopfile[i], O_WRONLY | O_APPEND | O_CREAT, 0600);
	    break;
	}
    }
}
#endif
