#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "kernel/stdio.h"
#include "filesys/buffer.h"

#include "filesys/directory.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <threads/fixed-point.h>

static void syscall_handler (struct intr_frame *);

static struct fd_pair* get_file_pair(int fd, struct list *fd_list);
static int find_free_fd (struct list *fd_list, struct file *file_pointer,
  struct dir *dir_pointer, bool is_dir);
static bool is_valid_pointer (uint32_t *pd, void* buffer, int32_t size);
static bool is_args_valid(int num_args, uint32_t* args);
static int32_t sys_write_handler (int fd, void* buffer, int32_t size);
static int32_t sys_read_handler (int fd, void* buffer, int32_t size);
static int32_t sys_open_handler (char *name);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  if(!is_args_valid(1, args))
  {
    f->eax = -1;
    sys_exit_handler(-1);
  }
  if (args[0] == SYS_PRACTICE)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    f->eax = args[1] + 1;

  }
  else if (args[0] == SYS_EXIT)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    f->eax = args[1];
    sys_exit_handler(args[1]);

  }
  else if (args[0] == SYS_READ)
  {
    if(!is_args_valid(4, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    int fd = (int) (args[1]);
    char *buffer = (void *) (args[2]);
    int32_t size = (int32_t) (args[3]);
    int32_t read_num = sys_read_handler(fd, buffer, size);
    f->eax = read_num;
    if (read_num == -1) sys_exit_handler(-1);

  }
  else if (args[0] == SYS_WRITE)
  {
    if(!is_args_valid(4, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    int fd = (int) (args[1]);
    char *buffer = (void *) (args[2]);
    size_t size = (size_t) (args[3]);
    int32_t write_num = sys_write_handler(fd, buffer, size);
    f->eax = write_num;
    if (write_num == -1) sys_exit_handler(-1);

  }
  else if (args[0] == SYS_CREATE)
  {
    if(!is_args_valid(3, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    char* file = (char*) (args[1]);
    unsigned size = (unsigned) (args[2]);
    struct thread *t = thread_current ();
    uint32_t *pd = t->pagedir;
    if (is_valid_pointer(pd, file, 0))
    {
      bool is_success = filesys_create (file, size);
      f->eax = is_success;
    }
    else
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }

  }
  else if (args[0] == SYS_OPEN)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    char* name = (char*) (args[1]);
    int fd = sys_open_handler(name);
    f->eax = fd;
    // printf("inside open %d, %s\n", fd, name);
    if (fd == -1)
    {
      sys_exit_handler(-1);
    }
    else if (fd == -2)
    {
      f->eax = -1;
    }
  }
  else if (args[0] == SYS_FILESIZE)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    int fd = (int)args[1];
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair == NULL)
    {
      f->eax = -1;
    }
    else
    {
      f->eax = file_length(fd_find_pair->f);
    }

  }
  else if (args[0] == SYS_SEEK)
  {
    if(!is_args_valid(3, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    int fd = (int) (args[1]);
    size_t position = (size_t) (args[2]);
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair != NULL)
    {
      file_seek (fd_find_pair->f, position);
    }

  }
  else if (args[0] == SYS_TELL)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    int fd = (int) (args[1]);
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair != NULL)
    {
      size_t res = file_tell (fd_find_pair->f);
      f->eax = res;
    }
    else
    {
      f->eax = -1;
    }

  }
  else if (args[0] == SYS_CLOSE)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    int fd = (int) (args[1]);
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair != NULL)
    {
      list_remove(&fd_find_pair->fd_elem);
      file_close(fd_find_pair->f);
      free(fd_find_pair);
    }

  }
  else if (args[0] == SYS_REMOVE)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    char* name = (char*) (args[1]);
    struct thread *t = thread_current ();
    uint32_t *pd = t->pagedir;
    if (is_valid_pointer(pd, name, 0))
    {
      // printf("removing %s\n", name);
      bool is_success = filesys_remove (name);
      f->eax = is_success;
    }
    else
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }

  }
  else if (args[0] == SYS_EXEC)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    char* cmd_line = (char*) (args[1]);
    struct thread *parent = thread_current();
    void *tmp = pagedir_get_page (parent->pagedir, (void *)cmd_line);
    if (tmp)
    {
      tid_t tid = process_execute(cmd_line);
      struct wait_status *child_wait_status = get_child_by_tid(parent, tid);
      f->eax = (child_wait_status->load_code == -1) ? -1 : (uint32_t)tid;
    }
    else
    {
      f->eax = -1;
    }

  }
  else if (args[0] == SYS_WAIT)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    tid_t tid = (int) (args[1]);
    f->eax = process_wait(tid);
  }
  else if (args[0] == SYS_MKDIR)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    char* name = (char*) (args[1]);
    struct thread *t = thread_current ();
    uint32_t *pd = t->pagedir;
    bool is_valid = is_valid_pointer(pd, name, 0);
    if (!is_valid) {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    // printf("inside mkdir, %s\n", name);
    bool res = filesys_create_dir (name);
    f->eax = res;
  }
  else if (args[0] == SYS_CHDIR)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    char* name = (char*) (args[1]);
    struct thread *t = thread_current ();
    uint32_t *pd = t->pagedir;
    bool is_valid = is_valid_pointer(pd, name, 0);
    if (!is_valid) {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    struct dir *dir = filesys_open_directory (name);
    if (dir == NULL) {
      f->eax = false;
    } else {
      if (t->curr_dir != NULL) {
        dir_close(t->curr_dir);
        t->curr_dir = dir;
        f->eax = true;
      }
    }
  }
  else if (args[0] == SYS_READDIR)
  {
    // check the validation of the pointer
    if(!is_args_valid(3, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    char* name = (char*) (args[2]);
    int fd = (int) (args[1]);
    struct thread *t = thread_current ();
    uint32_t *pd = t->pagedir;
    if (is_valid_pointer(pd, name, 0))
    {
      // OPEN AND READ THE DIR ENTRY
      struct list *fd_list = &(t->fd_list);
      struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
      bool is_success = false;
      // printf("here1: %d\n", fd);
      if (fd_find_pair->is_dir) {
        // printf("inside readdir\n");
        is_success = dir_readdir (fd_find_pair->d, name);
      }
      f->eax = is_success;
    }
    else
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
  }
  else if (args[0] == SYS_ISDIR)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    int fd = (int) (args[1]);
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair->is_dir) {
      f->eax = true;
    } else {
      f->eax = false;
    }
  }
  else if (args[0] == SYS_INUMBER)
  {
    if(!is_args_valid(2, args))
    {
      f->eax = -1;
      sys_exit_handler(-1);
    }
    int fd = (int) (args[1]);
    struct thread *t = thread_current ();
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    if (fd_find_pair->is_dir) {
      f->eax = inode_get_inumber (fd_find_pair->d->inode);
    } else {
      f->eax = inode_get_inumber (fd_find_pair->f->inode);
    }
  }
  else if (args[0] == SYS_BUFFER_CLEAN)
  {
      buffer_clean();
  }
  else if (args[0] == SYS_BUFFER_HIT_RATE)
  {
      int access;
      int hit;
      buffer_performance(&access, &hit);
      f->eax = fix_round(fix_mul(fix_div(fix_int(hit), fix_int(access)), fix_int(10000)));
  }
  else if (args[0] == SYS_BUFFER_READ_NUM)
  {
      f->eax = get_fs_device_read_cnt(fs_device);
  }
  else if (args[0] == SYS_BUFFER_WRITE_NUM)
  {
      f->eax = get_fs_device_write_cnt(fs_device);
  }
}

/**
 *  function to handle the exit call, it will delete all fd
 *  structs opened by this process. And call thread exit.
 **/

void
sys_exit_handler (int status)
{
  printf("%s: exit(%d)\n", &thread_current ()->name, status);
  // Close all the files
  struct thread *cur = thread_current();
  struct list *fd_list = &(cur->fd_list);
  struct list_elem *e = list_begin (fd_list);
  while (e !=  list_end(fd_list))
  {
    struct list_elem *tmp = e;
    e = list_remove(e);
    struct fd_pair *pair = list_entry (tmp, struct fd_pair, fd_elem);
    file_close(pair->f);
    free(pair);
  }
  thread_current()->wait_status->exit_code = status;
  thread_exit();
}

/**
 *  function to handle the read call, it will check the validation
 *  of the buffer, and it will iterate through the fd list to
 *  find the file. When it try read, a lock is acquired.
 **/

static int32_t
sys_read_handler (int fd, void* buffer, int32_t size)
{
  struct thread *t = thread_current ();
  uint32_t *pd = t->pagedir;
  bool is_valid = is_valid_pointer(pd, buffer, size);
  int32_t read_num = 0;
  if (is_valid)
  {
    if (fd == 0)
    {
      int i = 0;
      for (i = 0; i < size; i++)
      {
        *((char*)buffer + i) = input_getc();
      }
    }
    else
    {
      struct list *fd_list = &(t->fd_list);
      struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
      if (fd_find_pair->is_dir) {
        return -1;
      }
      read_num = file_read (fd_find_pair->f, buffer, size);
    }
  }
  else
  {
    read_num = -1;
  }
  return read_num;
}

/**
 *  function to handle the write call, it will check the validation
 *  of the bufferr. Then we iterate the fd list to find the
 *  file to call file_write. When call file_write, a lock is
 *  acquired.
 **/

static int32_t
sys_write_handler (int fd, void* buffer, int32_t size)
{
  struct thread *t = thread_current ();
  uint32_t *pd = t->pagedir;
  int32_t write_num = 0;
  // printf("inside sys write, %d\n", fd);
  if (!is_valid_pointer (pd, buffer, size))
  {
    write_num = -1;
    return write_num;
  }
  if (fd == 1)
  {
    putbuf(buffer, size);
    write_num = size;
  }
  else
  {
    struct list *fd_list = &(t->fd_list);
    struct fd_pair *fd_find_pair = get_file_pair(fd, fd_list);
    // if it's a dir then return -1
    if (fd_find_pair->is_dir) {
      return -1;
    }
    if (fd_find_pair != NULL)
    {
      size_t size_write = file_write (fd_find_pair->f, buffer, size);
      write_num = size_write;
    }
    else
    {
      write_num = 0;
    }
  }
  return write_num;
}

/**
 *  function to handle the open call, it will check the name pointer
 *  is valid. If so, it open the file, and find a smallest fd number
 *  to return. Meanwhile add the fd struct to the list.
 **/
 // this need to open either file or dir
 // when open a thing, first try the name of file, if failed try dir
static int32_t
sys_open_handler (char *name)
{
  struct thread *t = thread_current ();
  uint32_t *pd = t->pagedir;
  bool is_valid = is_valid_pointer(pd, name, 0);
  if (is_valid)
  {
    struct file *file_pointer = filesys_open (name);
    struct list *fd_list = &(t->fd_list);
    if (file_pointer == NULL) {
      // try open the dir
      struct dir *dir_pointer = filesys_open_directory (name);
      if (dir_pointer == NULL) {
        return -2;
      } else {
        // it's a dir
        int res_fd = find_free_fd(fd_list, NULL, dir_pointer, true);
        return(res_fd);
      }
    } else {
      // it's a file
      int res_fd = find_free_fd(fd_list, file_pointer, NULL, false);
      return(res_fd);
    }
  }
  else
  {
    return -1;
  }
}

/**
 *  function to check the validatio of args. It will check
 *  num_args of args's pointer.
 **/
static bool
is_args_valid(int num_args, uint32_t* args)
{
  struct thread *curr_t = thread_current ();
  uint32_t *curr_pd = curr_t->pagedir;
  int i = 0;
  for (i = 0; i < num_args; i++)
  {
    if(!is_valid_pointer(curr_pd, &args[i], 0))
    {
      return false;
    }
  }
  return true;
}

/**
 *  function to find the fd_pair whose fd is the same
 *  as the fd, from the fd_list.
 **/
static struct fd_pair*
get_file_pair(int fd, struct list *fd_list)
{
  struct list_elem *e;
  for (e = list_begin (fd_list); e != list_end (fd_list);
       e = list_next (e))
  {
    struct fd_pair *pair = list_entry (e, struct fd_pair, fd_elem);
    if (pair->fd == fd)
    {
      return pair;
    }
  }
  return NULL;
}

/**
 *  function to check the validation of a memroy start from
 *  buffer, and has length size. We check every 4 KB.
 **/
static bool
is_valid_pointer (uint32_t *pd, void* buffer, int32_t size)
{
  if (buffer == NULL) return false;
  int i = 0;
  for (i = 0; i <= size /4000; i++)
  {
    char *add = (char *)buffer + i * 4000;
    bool is_user = is_user_vaddr((void *)add);
    if (!is_user)
    {
      return false;
    }
    void *tmp = pagedir_get_page (pd, (void *)add);
    if (tmp == NULL)
    {
      return false;
    }
  }
  return true;
}

/**
 *  function to find the fd_pair which contains file_pointer,
 *  delete the fd_pair struct from fd_list.
 **/
static int
find_free_fd (struct list *fd_list, struct file *file_pointer,
  struct dir *dir_pointer, bool is_dir)
{
  struct list_elem *e, *prev_elem;
  int fd_prev = -1;

  struct fd_pair *insert_pair = (struct fd_pair *) malloc(sizeof(struct fd_pair));
  if (is_dir) {
    insert_pair->d = dir_pointer;
    insert_pair->is_dir = true;
    insert_pair->f = NULL;
  } else {
    insert_pair->f = file_pointer;
    insert_pair->is_dir = false;
    insert_pair->d = NULL;
  }
  

  for (e = list_begin (fd_list); e != list_end (fd_list);
       e = list_next (e))
  {
    struct fd_pair *pair = list_entry (e, struct fd_pair, fd_elem);
    if (fd_prev == -1 && pair->fd == 2)
    {
      fd_prev = pair->fd;
      prev_elem = e;
      continue;
    }
    else if (fd_prev == -1 && pair->fd > 2)
    {
      // insert as fd = 2
      insert_pair->fd = 2;
      list_push_front (fd_list, &insert_pair->fd_elem);
      return 2;
    }
    if (pair->fd > fd_prev + 1)
    {
      // then we should insert here
      insert_pair->fd = fd_prev + 1;
      list_insert (prev_elem, &insert_pair->fd_elem);
      return insert_pair->fd;
    }
    // update the prev
    fd_prev = pair->fd;
    prev_elem = e;
  }
  if (fd_prev != -1)
  {
    insert_pair->fd = fd_prev + 1;
    list_push_back (fd_list, &insert_pair->fd_elem);
  }
  else
  {
    insert_pair->fd = 2;
    list_push_back (fd_list, &insert_pair->fd_elem);
  }
  return insert_pair->fd;
}
