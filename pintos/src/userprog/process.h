#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>
#include "filesys/file.h"
#include "synch.h"

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

struct wait_status
{
	struct list_elem elem; /* children list element */
	struct lock ref_cnt_lock; /* lock to protect ref_cnt */
	int ref_cnt; /* 2=child and parent both alive, 1=either child or parent alive */
	tid_t tid; /* child thread id */
	int exit_code; /* exit code, if dead */
	struct semaphore dead /* 1=child_alive, 0=child_dead */
};

#endif /* userprog/process.h */
