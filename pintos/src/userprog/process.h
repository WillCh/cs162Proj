#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>
#include "filesys/file.h"

#include "threads/thread.h"


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);


struct fd_pair
{
	struct file *f;
	int fd;
	struct list_elem fd_elem;
};

#endif /* userprog/process.h */
