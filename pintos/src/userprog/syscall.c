#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "kernel/stdio.h"

static void syscall_handler (struct intr_frame *);
static bool is_valid_pointer (uint32_t *pd, void* buffer, int32_t size);
static int32_t sys_read_handler (int fd, void* buffer, int32_t size);
static int32_t sys_open_handler (char *name);
static int find_free_fd (struct list *fd_list, struct file *file_pointer);
static struct fd_pair* get_file_pair(int fd, struct list *fd_list);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t* args = ((uint32_t*) f->esp);
  //printf("System call number: %d\n", args[0]);
  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    sys_exit_handler(args[1]);

  } else if (args[0] == SYS_READ) {
  	int fd = (int) (args[1]);
    char *buffer = (void *) (args[2]);
    int32_t size = (int32_t) (args[3]);
    int32_t read_num = sys_read_handler(fd, buffer, size);
    f->eax = read_num;
    if (read_num == -1) sys_exit_handler(-1);

  } else if (args[0] == SYS_WRITE) {
    // get the param from the stack
    int fd = (int) (args[1]);
    char *buffer = (void *) (args[2]);
    size_t size = (size_t) (args[3]);
    struct thread *t = thread_current ();
    uint32_t *pd = t->pagedir;
    // check the buffer is valid
    if (!is_valid_pointer (pd, buffer, size)) {

      f->eax = -1;
      sys_exit_handler(-1);
    } 

    if (fd == 1) {
      putbuf(buffer, size);
      f->eax = size;
    } else {
      
      struct list *fd_list = &(t->fd_list);
      struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
      if (fd_find_pair != NULL) {
        // lock
        lock_acquire(&(fd_find_pair->f->file_lock));
        size_t size_write = file_write (fd_find_pair->f, buffer, size);
        lock_release(&(fd_find_pair->f->file_lock));
        f->eax = size_write; 
      } else {
        f->eax = 0;
      }
    }

  } else if (args[0] == SYS_CREATE) {
    char* file = (char*) (args[1]);
    unsigned size = (unsigned) (args[2]);
    struct thread *t = thread_current ();
    uint32_t *pd = t->pagedir;
    if (is_valid_pointer(pd, file, 0)) {
      bool is_success = filesys_create (file, size);
      f->eax = is_success;
    } else {
      f->eax = -1;
      sys_exit_handler(-1);
    }

  } else if (args[0] == SYS_OPEN) {
    char* name = (char*) (args[1]);
    int fd = sys_open_handler(name);
    f->eax = fd;
    
    if (fd == -1) {
      sys_exit_handler(-1);
    } else if (fd == -2) {
      f->eax = -1;
    }
  } else if (args[0] == SYS_FILESIZE) {
    int fd = (int)args[1];
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair == NULL) {
      f->eax = -1;
    } else {
      lock_acquire(&(fd_find_pair->f->file_lock));
      f->eax = file_length(fd_find_pair->f);
      lock_release(&(fd_find_pair->f->file_lock));
    }

  } else if (args[0] == SYS_SEEK) {
    int fd = (int) (args[1]);
    size_t position = (size_t) (args[2]);
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair != NULL) {
      lock_acquire(&(fd_find_pair->f->file_lock));
      file_seek (fd_find_pair->f, position);
      lock_release(&(fd_find_pair->f->file_lock));
    } 

  } else if (args[0] == SYS_TELL)  {
    int fd = (int) (args[1]);
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair != NULL) {
      lock_acquire(&(fd_find_pair->f->file_lock));
      size_t res = file_tell (fd_find_pair->f);
      lock_release(&(fd_find_pair->f->file_lock));
      f->eax = res;
    } else {
      f->eax = -1;
    }

  } else if (args[0] == SYS_CLOSE) {
    int fd = (int) (args[1]);
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair != NULL) {
      // only delete the fd_pair here
      list_remove(&fd_find_pair->fd_elem);
      free(fd_find_pair);
    }

  } else if (args[0] == SYS_REMOVE) {
    char* name = (char*) (args[1]);
    struct thread *t = thread_current ();
    uint32_t *pd = t->pagedir;
    if (is_valid_pointer(pd, name, 0)) {
      bool is_success = filesys_remove (name);
      f->eax = is_success;
    } else {
      f->eax = -1;
      sys_exit_handler(-1);
    }
  }


}


static struct fd_pair*
get_file_pair(int fd, struct list *fd_list) {
  struct list_elem *e;
  for (e = list_begin (fd_list); e != list_end (fd_list);
       e = list_next (e)) {
    struct fd_pair *pair = list_entry (e, struct fd_pair, fd_elem);
    if (pair->fd == fd) {
      return pair;
    }
  }
  return NULL;
}

static bool
is_valid_pointer (uint32_t *pd, void* buffer, int32_t size) {
  if (buffer == NULL) return false;
  int i = 0;
  for (i = 0; i <= size /4000; i++) {
    char *add = (char *)buffer + i * 4000;
    bool is_user = is_user_vaddr((void *)add);
    if (!is_user) {
      return false;
    }
    void *tmp = pagedir_get_page (pd, (void *)add);
    if (tmp == NULL) {
      return false;
    }
  }
  return true;
}


void
sys_exit_handler (int status) {
  printf("%s: exit(%d)\n", &thread_current ()->name, status);
  thread_exit();
}

static int32_t 
sys_read_handler (int fd, void* buffer, int32_t size) {
  struct thread *t = thread_current ();
  uint32_t *pd = t->pagedir;
  bool is_valid = is_valid_pointer(pd, buffer, size);
  int32_t read_num = 0;
  if (is_valid) {
    if (fd == 0) {
      int i = 0;
      for (i = 0; i < size; i++) {
        *((char*)buffer + i) = input_getc();
      }
    } else {
      // find the fd_struct from the thread's list
      struct list *fd_list = &(t->fd_list);
      struct list_elem *e;

      for (e = list_begin (fd_list); e != list_end (fd_list);
           e = list_next (e)) {
        struct fd_pair *pair = list_entry (e, struct fd_pair, fd_elem);
        if (pair->fd == fd) {
          struct file *file_pointer = pair->f;
          lock_acquire(&file_pointer->file_lock);
          read_num = file_read (file_pointer, buffer, size); 
          lock_release(&file_pointer->file_lock);
          break;
        }
      }
    }
    
  } else {
    read_num = -1;
  }
  return read_num;
}

static int32_t 
sys_open_handler (char *name) {
  struct thread *t = thread_current ();
  uint32_t *pd = t->pagedir;
  bool is_valid = is_valid_pointer(pd, name, 0);
  if (is_valid) {
    struct file *file_pointer = filesys_open (name);
    if (file_pointer == NULL) return -2;
    struct list *fd_list = &(t->fd_list);
    int res_fd = find_free_fd(fd_list, file_pointer);
    return(res_fd);
  } else {
    return -1;
  }
} 

static int
find_free_fd (struct list *fd_list, struct file *file_pointer) {
  struct list_elem *e, *prev_elem;
  int fd_prev = -1;
  struct fd_pair *insert_pair = (struct fd_pair *) malloc(sizeof(struct fd_pair));
  insert_pair->f = file_pointer;

  for (e = list_begin (fd_list); e != list_end (fd_list);
       e = list_next (e)) {
    struct fd_pair *pair = list_entry (e, struct fd_pair, fd_elem);
    if (fd_prev == -1 && pair->fd == 2) {
      fd_prev = pair->fd;
      prev_elem = e;
      continue;
    } else if (fd_prev == -1 && pair->fd > 2) {
      // insert as fd = 2
      insert_pair->fd = 2;
      list_push_front (fd_list, &insert_pair->fd_elem);
      return 2;
    }
    if (pair->fd > fd_prev + 1) {
      // then we should insert here
      insert_pair->fd = fd_prev + 1;
      list_insert (prev_elem, &insert_pair->fd_elem);
      return insert_pair->fd;
    }
    // update the prev
    fd_prev = pair->fd;
    prev_elem = e;
  }
  if (fd_prev != -1) {
    insert_pair->fd = fd_prev + 1;
    list_push_back (fd_list, &insert_pair->fd_elem);
  } else {
    insert_pair->fd = 2;
    list_push_back (fd_list, &insert_pair->fd_elem);
  }
  return insert_pair->fd;
}