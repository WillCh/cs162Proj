#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

#include "filesys/buffer.h"
#include "threads/thread.h"
/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
  const char *src = *srcp;
  char *dst = part;
  /* Skip leading slashes. If it’s all slashes, we’re done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;
  if (*src == '\0') {
    return 0;
  }
  return 1;
}

bool
filesys_create_dir (const char *name) {
  block_sector_t inode_sector = 0;
  int initial_size = 512;
  struct dir *dir = filesys_curr_dir();
  // ABSOLUTE PATH
  if (name[0] == '/') {
    dir = dir_open_root ();
  } else {
    // check current dir is valid
    if (dir->is_remove) {
      return false;
    }
    dir = dir_reopen (dir);
  }

  char part[NAME_MAX + 1];
  part[0] = 0;
  struct dir_entry entry;
  bool success = true;
  while (get_next_part(part, &name) == 1) {
    if (entry_lookup(dir, part, &entry, NULL, 1)) {
      struct inode *inode = inode_open (entry.inode_sector);
      dir_close (dir);
      dir = dir_open (inode);
    } else {
      success = false;
      break;
    }
  }
  if (success) {
    success = (dir != NULL
      && free_map_allocate (1, &inode_sector)
      && inode_create (inode_sector, initial_size)
      && dir_add_directory (dir, part, inode_sector, true));
  //    && dir_add (dir, part, inode_sector));    
  }
  // find the parents inode_sector

  // write the ../ dir_entry back to the inode
  // printf("here %d, the entry name is %s, the cur name is %s\n",
  //  success, entry.name, part);
  if (success) {
    char name1[3];
    name1[0] = '.'; name1[1] = '.'; name1[2] = 0;
    char name2[2];
    name2[0] = '.'; name2[1] = 0;
    entry.inode_sector = inode_get_inumber(dir->inode);
    entry.is_dir = true;
    strlcpy (&(entry.name), name1, strlen (name1) + 1);
    // printf("finish str cpy %s, the sect num is %d\n",
    //  entry.name, entry.inode_sector);
    struct inode *inode = inode_open (inode_sector);
    // printf("finish inode open\n");
    inode_write_at(inode, &entry, sizeof (entry), 0);
    // printf("finish the 1st write\n");
    // write the ./ dir_entry back to the inode
    strlcpy (&(entry.name), name2, strlen (name2) + 1);
    entry.inode_sector = inode_sector;

    inode_write_at(inode, &entry, sizeof (entry), sizeof (entry));
    inode_close (inode);
  }
  
  // printf("fisnih create file, the sector is %d\n", inode_sector);
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  return success;
}


/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  // added by haoyu 
  // init the buffer
  buffer_init();
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  buffer_update_disk ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  // printf("inside file sys create, file name is %s, the size is %d\n",
  //  name, initial_size);
  block_sector_t inode_sector = 0;
  struct dir *dir = filesys_curr_dir();
  // ABSOLUTE PATH
  if (name[0] == '/') {
    dir = dir_open_root ();
  } else {
    // check current dir is valid or not
    if (dir->is_remove) {
      return false;
    }
    dir = dir_reopen (dir);
  }

  char part[NAME_MAX + 1];
  part[0] = 0;
  struct dir_entry entry;
  bool success = true;
  while (get_next_part(part, &name) == 1) {
    if (entry_lookup(dir, part, &entry, NULL, 1)) {
      struct inode *inode = inode_open (entry.inode_sector);
      dir_close (dir);
      dir = dir_open (inode);
    } else {
      success = false;
      break;
    }
  }

  if (success) {
    success = (dir != NULL
      && free_map_allocate (1, &inode_sector)
      && inode_create (inode_sector, initial_size)
      && dir_add (dir, part, inode_sector));    
  }

  // printf("fisnih create file, the sector is %d\n", inode_sector);
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct inode *inode = NULL;
  struct dir *dir = filesys_curr_dir();
  // ABSOLUTE PATH
  if (name[0] == '/') {
    dir = dir_open_root ();
  } else {
    // check the current dir is valid or deleted

    if (dir->is_remove) {
      return false;
    }
    dir = dir_reopen (dir);
  }
  if (!strcmp ("/", name)) {
    return NULL;
  }
  char part[NAME_MAX + 1];
  struct dir_entry entry;
  bool success = true;
  while (get_next_part(part, &name) == 1) {
    if (entry_lookup(dir, part, &entry, NULL, 1)) {
      struct inode *inode = inode_open (entry.inode_sector);
      dir_close (dir);
      dir = dir_open (inode);
    } else {
      success = false;
      break;
    }
  }
  // printf("inside filesys_open, the name is %s\n", part);
  if (success) {
    dir_lookup_files (dir, part, &inode);
  } else {
    dir_close (dir);
    return NULL;
  }
  dir_close (dir);
  return file_open (inode);
}

struct dir *
filesys_open_directory (const char *name)
{
  struct inode *inode = NULL;
  struct dir *dir = filesys_curr_dir();
  // ABSOLUTE PATH
  if (name[0] == '/') {
    dir = dir_open_root ();
  } else {
    // check whether the current dir is delete or not
    if (dir->is_remove) {
      return false;
    }
    dir = dir_reopen (dir);
  }
  if (!strcmp ("/", name)) {
    return dir_open_root ();
  }
  char part[NAME_MAX + 1];
  struct dir_entry entry;
  bool success = true;
  while (get_next_part(part, &name) == 1) {
    if (entry_lookup(dir, part, &entry, NULL, 1)) {
      struct inode *inode = inode_open (entry.inode_sector);
      dir_close (dir);
      dir = dir_open (inode);
    } else {
      success = false;
      break;
    }
  }

  if (success && entry_lookup(dir, part, &entry, NULL, 1)) {
    struct inode *inode = inode_open (entry.inode_sector);
    dir_close (dir);
    dir = dir_open (inode);
  } else {
    dir_close (dir);
    dir = NULL;
  }

  return dir;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = filesys_curr_dir();
  // ABSOLUTE PATH
  // printf("filesys remove %s\n", name);
  if (name[0] == '/') {
    dir = dir_open_root ();
  } else {
    // check valid of current dir
    if (dir->is_remove) {
      // printf("cur is closed\n");
      return false;
    }
    dir = dir_reopen (dir);
  }
  // printf("here\n");
  char part[NAME_MAX + 1];
  part[0] = 0;
  struct dir_entry entry;
  bool success = true;
  while (get_next_part(part, &name) == 1) {
    if (entry_lookup(dir, part, &entry, NULL, 1)) {
      struct inode *inode = inode_open (entry.inode_sector);
      dir_close (dir);
      dir = dir_open (inode);
    } else {
      success = false;
      break;
    }
  }

  if (success) {
    // printf("here2\n");
    success = dir != NULL && dir_remove (dir, part);
  }
  dir_close (dir);
  return success;  
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  // printf ("done.\n");
}

struct dir *
filesys_curr_dir (void) {
  struct thread *t = thread_current();
  if (t->curr_dir == NULL) {
    t->curr_dir = dir_open_root ();
  }
  return t->curr_dir;
}

