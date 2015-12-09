#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/off_t.h"


/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
    struct lock lock_cnf; /* protect the cnf */
    int cnf;      /* count the number of process open this dir*/
  	struct lock lock_item; /* protect the item_cnf */
    int item_cnf;  /* count number of files in this dir */
    bool is_remove; 
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
    bool is_dir;      /* is a dir or a file */
    struct lock lock_del; /* lock to prevent data racing for multiple delete */    
  };

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);
bool entry_lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp, bool is_dir);
bool dir_add_directory (struct dir *dir, const char *name,
   block_sector_t inode_sector, bool is_dir);
int dir_sizeof (struct dir_entry *dir_entry);
bool dir_lookup_files (const struct dir *dir, const char *name,
            struct inode **inode);

#endif /* filesys/directory.h */
