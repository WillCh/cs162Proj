
#include "tests/main.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall.h>
#include "tests/filesys/extended/mk-tree.h"
#include "tests/lib.h"

void
test_main (void) 
{
  create ("a", 16*512);
  int fd = open("a");
  char buffer[512];
  memset(buffer, 1, 512);
  buffer_clean();

  int i;
  for (i = 0; i < 16; ++i)
    write(fd, buffer, 512);
  int num_read_before = buffer_read_num();
  int num_write_before = buffer_write_num();

  seek(fd, 0);
  for (i = 0; i < 16; ++i)
    write(fd, buffer, 512);
  int num_read_middle = buffer_read_num();
  int num_write_middle = buffer_write_num();

  memset(buffer, 2, 512);

  seek(fd, 0);
  for (i = 0; i < 16; ++i)
    write(fd, buffer, 512);
  buffer_clean();
  int num_read_end = buffer_read_num();
  int num_write_end = buffer_write_num();

  int read_diff_middle_before = num_read_middle - num_read_before;
  int write_diff_middle_before = num_write_middle - num_write_before;
  int read_diff_end_middle = num_read_end - num_read_middle;
  int write_diff_end_middle = num_write_end - num_write_middle;
  
  msg("read_diff_middle_before: %d", read_diff_middle_before);
  msg("write_diff_middle_before: %d", write_diff_middle_before);
  msg("read_diff_end_middle: %d", read_diff_end_middle);
  msg("write_diff_end_middle: %d", write_diff_end_middle);

  close(fd);
}

