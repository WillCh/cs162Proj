
#include "tests/main.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall.h>
#include "tests/filesys/extended/mk-tree.h"
#include "tests/lib.h"

void
test_main (void) 
{
  char file[128];

  create ("a", 10000);
  buffer_clean();
  int fd = open("a");
  char buffer[512];
  int i;
  for (i = 0; i < 10000/512; ++i)
    read(fd, buffer, 512);
  int hit_rate_before = buffer_hit_rate();
  close(fd);
  fd = open("a");
  for (i = 0; i < 10000/512; ++i)
    read(fd, buffer, 512);
  int hit_rate_after = buffer_hit_rate();
  close(fd);
  if (hit_rate_before <= hit_rate_after){
      msg("Hit rate improves");
  }
  else{
      msg("Hit rate worsens");
  }
}

