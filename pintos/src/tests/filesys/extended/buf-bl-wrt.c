
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
  int num_read_before = buffer_read_num(); //should be zero
  int num_write_before = buffer_write_num(); //should be zero
  printf("number of reads before: %d\n", num_read_before);
  printf("number of wirtes before: %d\n", num_write_before);

  seek(fd, 0);
  for (i = 0; i < 16; ++i)
    write(fd, buffer, 512);
  int num_read_middle = buffer_read_num(); //should be zero
  int num_write_middle = buffer_write_num(); //should be zero
  printf("number of reads middle: %d\n", num_read_middle);
  printf("number of wirtes middle: %d\n", num_write_middle);

  seek(fd, 0);
  for (i = 0; i < 16; ++i)
    write(fd, buffer, 512);
  int num_read_end = buffer_read_num(); //should be zero
  int num_write_end = buffer_write_num(); //should be 16
  printf("number of reads end: %d\n", num_read_end);
  printf("number of wirtes end: %d\n", num_write_end);

  close(fd);
}

