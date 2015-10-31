#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t* args = ((uint32_t*) f->esp);
  // printf("System call number: %d\n", args[0]);
  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf("%s: exit(%d)\n", &thread_current ()->name, args[1]);
    thread_exit();
  } else if (args[0] == SYS_READ) {
  	// int fd = ;

  	// file = filesys_open()
  } else if (args[0] == SYS_WRITE) {
    // get the param from the stack
    int fd = (int) (args[1]);
    char *buffer = (void *) (args[2]);
    size_t size = (size_t) (args[3]);
    int i = 0;
    for (i = 0; i < size; i++) {
      printf("%c", *(buffer+i));
    }
  	//printf("fd is %d; buffer is %p; size is %zu\n", fd, buffer, size);
  }
}

