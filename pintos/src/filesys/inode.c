#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/buffer.h"
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIR_LEN 123
#define MAX_LEN ((123 + 2 * BLOCK_SECTOR_SIZE / 4 + BLOCK_SECTOR_SIZE / 4 * BLOCK_SECTOR_SIZE / 4) * BLOCK_SECTOR_SIZE)

static block_sector_t
byte_to_sector_helper (const struct inode *inode, off_t pos, int len);
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t dir[DIR_LEN];
    block_sector_t single_indir[2];
    block_sector_t double_indir;
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock extend_lock;
    struct lock length_lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  int len = inode_length (inode);
  return byte_to_sector_helper (inode, pos, len);
}

static block_sector_t
byte_to_sector_helper (const struct inode *inode, off_t pos, int len) 
{
  ASSERT (inode != NULL);
  if (pos < len) {
    int index = pos / BLOCK_SECTOR_SIZE;
    if (index < DIR_LEN) {
      return inode->data.dir[index];
    } else if (index < DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
      // 1 level indir case
      // read from the device
      block_sector_t *tmp_buf = calloc(1, BLOCK_SECTOR_SIZE);
      buffer_read (fs_device, inode->data.single_indir[0], tmp_buf, 0, BLOCK_SECTOR_SIZE);
      block_sector_t res = tmp_buf[index - DIR_LEN];
      free(tmp_buf);
      return res;

    } else if (index < DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
      // 2nd 1 level indir case
      block_sector_t *tmp_buf = calloc(1, BLOCK_SECTOR_SIZE);
      buffer_read (fs_device, inode->data.single_indir[1], tmp_buf, 0, BLOCK_SECTOR_SIZE);
      block_sector_t res = tmp_buf[index - DIR_LEN - BLOCK_SECTOR_SIZE / 4];
      free(tmp_buf);
      return res;

    } else { 
      // read the double indirector pointer
      block_sector_t *tmp_buf = calloc(1, BLOCK_SECTOR_SIZE);
      buffer_read (fs_device, inode->data.double_indir, tmp_buf, 0, BLOCK_SECTOR_SIZE);
      index = index - (DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4);
      int tmp_index = index / (BLOCK_SECTOR_SIZE / 4);
      int tmp_index2 = index % (BLOCK_SECTOR_SIZE / 4);
      block_sector_t id_2level = tmp_buf[tmp_index];
      buffer_read (fs_device, id_2level, tmp_buf, 0, BLOCK_SECTOR_SIZE);
      block_sector_t res = tmp_buf[tmp_index2];
      free(tmp_buf);
      return res;
    }
  } else {
    return -1;    
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);
  if (length > MAX_LEN) {
    return success;
  }
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      int index = length / BLOCK_SECTOR_SIZE;
      int i = 0;
      char *zeros = calloc(1, BLOCK_SECTOR_SIZE);
      // static char zeros[BLOCK_SECTOR_SIZE];
      block_sector_t *tmpsect_1st = NULL;
      block_sector_t *tmpsect_2nd = NULL;
      block_sector_t *tmpsect_2level_1 = NULL;
      if (index >= DIR_LEN) {
        // printf("inside the inode create has indir pnt\n");
        // 1st indir
        success = free_map_allocate (1, &(disk_inode->single_indir[0]));
        tmpsect_1st = calloc(1, BLOCK_SECTOR_SIZE);
      } 
      if (index >= DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
        // 2nd indir
        success = free_map_allocate (1, &(disk_inode->single_indir[1]));
        tmpsect_2nd = calloc(1, BLOCK_SECTOR_SIZE);
      }
      if (index >= DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {

        success = free_map_allocate (1, &(disk_inode->double_indir));
        tmpsect_2level_1 = calloc(1, BLOCK_SECTOR_SIZE);
        int tmp_index = index - (DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4);
        int tmp_index2 = tmp_index / (BLOCK_SECTOR_SIZE / 4);
        for (i = 0; i <= tmp_index2; i++) {
          success = free_map_allocate (1, &(tmpsect_2level_1[i]));
        }
        buffer_write (fs_device, disk_inode->double_indir, tmpsect_2level_1, 0, BLOCK_SECTOR_SIZE);
      }
      // alloc the data sector
      block_sector_t *cur_sector = calloc(1, BLOCK_SECTOR_SIZE);
      for (i = 0; i <= index; i++) {
        if (i < DIR_LEN) {
          success = free_map_allocate (1, &(disk_inode->dir[i]));
          buffer_write (fs_device, disk_inode->dir[i], zeros, 0, BLOCK_SECTOR_SIZE);
        } else if (i < DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
          // 1st 1-level
          int index_1st_level = i - DIR_LEN;
          success = free_map_allocate (1, &(tmpsect_1st[index_1st_level]));
          buffer_write (fs_device, tmpsect_1st[index_1st_level] ,zeros, 0, BLOCK_SECTOR_SIZE);
        } else if (i < DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
          int index_1st_level = i - DIR_LEN - BLOCK_SECTOR_SIZE / 4;
          success = free_map_allocate (1, &(tmpsect_2nd[index_1st_level]));
          buffer_write (fs_device, tmpsect_2nd[index_1st_level] ,zeros, 0, BLOCK_SECTOR_SIZE);
        } else {
          // double director case
          int index_1st_level = i - DIR_LEN - 2 * BLOCK_SECTOR_SIZE / 4;
          int tmp_index = index_1st_level / (BLOCK_SECTOR_SIZE / 4);
          int tmp_index2 = index_1st_level % (BLOCK_SECTOR_SIZE / 4);
          
          if (tmp_index2 == 0) {
            // beginng of the 2-level page
            memset (cur_sector, 0, BLOCK_SECTOR_SIZE);
          }
          
          success = free_map_allocate (1, &(cur_sector[tmp_index2]));
          buffer_write (fs_device, cur_sector[tmp_index2], zeros, 0, BLOCK_SECTOR_SIZE);
          if (tmp_index2 == BLOCK_SECTOR_SIZE / 4 - 1) {
            // last entry of the 2-level page
            buffer_write (fs_device, tmpsect_2level_1[tmp_index], cur_sector, 0, BLOCK_SECTOR_SIZE);
          } else if (i == index) {
            // last entry
            buffer_write (fs_device, tmpsect_2level_1[tmp_index], cur_sector, 0, BLOCK_SECTOR_SIZE);
            // NOTICE, the last page may contain useless 0s
          }
        } 
      }
      // write back the page content to the disk
      if (tmpsect_1st != NULL) {
        buffer_write (fs_device, disk_inode->single_indir[0], tmpsect_1st, 0, BLOCK_SECTOR_SIZE);
      }
      if (tmpsect_2nd != NULL) {
        buffer_write (fs_device, disk_inode->single_indir[1], tmpsect_2nd, 0, BLOCK_SECTOR_SIZE);
      }
      free (tmpsect_1st);
      free (tmpsect_2nd);
      free (tmpsect_2level_1);
      free (cur_sector);
      free (zeros);
      // write back the inode
      buffer_write (fs_device, sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
      success = true; 
    }
    free (disk_inode);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL) {
    return NULL;
  }
    

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  // init the lock
  lock_init (&inode->extend_lock);
  lock_init (&inode->length_lock);
  buffer_read (fs_device, inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;   // inode num is the sector num
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          // i.e. we delete the file corresponding to this inode
          // free the inode itself on the disk
          free_map_release (inode->sector, 1);

          // remove every data
          struct inode_disk *disk_inode = &inode->data;
          int index = disk_inode->length / BLOCK_SECTOR_SIZE;
          int i = 0;
          block_sector_t *tmpsect_1st = NULL;
          block_sector_t *tmpsect_2nd = NULL;
          block_sector_t *tmpsect_2level_1 = NULL;

          // read and free the internal page
          if (index >= DIR_LEN) {
            // 1st indir
            tmpsect_1st = calloc(1, BLOCK_SECTOR_SIZE);
            buffer_read (fs_device, disk_inode->single_indir[0], tmpsect_1st, 0, BLOCK_SECTOR_SIZE);
            free_map_release (disk_inode->single_indir[0], 1);
          } 
          if (index >= DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
            // 2nd indir
            tmpsect_2nd = calloc(1, BLOCK_SECTOR_SIZE);
            buffer_read (fs_device, disk_inode->single_indir[1], tmpsect_2nd, 0, BLOCK_SECTOR_SIZE);
            free_map_release (disk_inode->single_indir[1], 1);
          }
          if (index >= DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
            tmpsect_2level_1 = calloc(1, BLOCK_SECTOR_SIZE);
            buffer_read (fs_device, disk_inode->double_indir, tmpsect_2level_1, 0, BLOCK_SECTOR_SIZE);
            free_map_release (disk_inode->double_indir, 1);
          }

          block_sector_t *cur_sector = calloc(1, BLOCK_SECTOR_SIZE);

          // free the data 
          for (i = 0; i <= index; i++) {
            if (i < DIR_LEN) {
              free_map_release (disk_inode->dir[i], 1);
            } else if (i < DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
              // 1st 1-level
              int index_1st_level = i - DIR_LEN;
              free_map_release (tmpsect_1st[index_1st_level], 1);
            } else if (i < DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
              int index_1st_level = i - DIR_LEN - BLOCK_SECTOR_SIZE / 4;
              free_map_release (tmpsect_2nd[index_1st_level], 1);
            } else {
              int index_1st_level = i - DIR_LEN - 2 * BLOCK_SECTOR_SIZE / 4;
              int tmp_index = index_1st_level / (BLOCK_SECTOR_SIZE / 4);
              int tmp_index2 = index_1st_level % (BLOCK_SECTOR_SIZE / 4);
              if (tmp_index2 == 0) {
                buffer_read (fs_device, tmpsect_2level_1[tmp_index], cur_sector, 0, BLOCK_SECTOR_SIZE);
              }
              free_map_release (cur_sector[tmp_index2], 1);
              if (tmp_index2 == BLOCK_SECTOR_SIZE / 4 - 1) {
                // we finish this 2-level page
                free_map_release (tmpsect_2level_1[tmp_index], 1);
              } else if (i == index) {
                // last case
                free_map_release (tmpsect_2level_1[tmp_index], 1);
              }
            }
          }
          free (tmpsect_1st);
          free (tmpsect_2nd);
          free (tmpsect_2level_1);
          free (cur_sector);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  off_t inode_len = inode_length (inode);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      //printf(" inside the inode read, the sector id is %d\n", sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_len - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      buffer_read (fs_device, sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  off_t original_size = size;
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  off_t newLen = size + offset;
  if (newLen > MAX_LEN) {
    return 0;
  }
  inode_extend_length (inode, size, offset);

  off_t inodeLen = inode_length (inode);
  newLen = inodeLen > newLen ? inodeLen : newLen;
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector_helper (inode, offset, newLen);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = newLen - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0) {
        break;
      }
      buffer_write (fs_device, sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
 
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // update the inode length
  struct inode_disk *disk_inode = &inode->data;
  lock_acquire (&inode->length_lock);
  disk_inode->length = newLen;
  buffer_write (fs_device, inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  lock_release (&inode->length_lock);
  free (bounce);
  return bytes_written;
}

/* Function to extend the inode */
void
inode_extend_length (struct inode *inode, off_t size,
                off_t offset)
{

  lock_acquire (&inode->extend_lock);
  int newLen = offset + size;
  int oldLen = inode_length(inode);
  if (oldLen < newLen) {
    struct inode_disk *disk_inode = &inode->data;
    int newindex = newLen / BLOCK_SECTOR_SIZE;
    int oldindex = oldLen / BLOCK_SECTOR_SIZE;
    char *zeros = calloc(1, BLOCK_SECTOR_SIZE);
    // static char zeros[BLOCK_SECTOR_SIZE];
    block_sector_t *tmpsect_1st = calloc(1, BLOCK_SECTOR_SIZE);
    block_sector_t *tmpsect_2nd = calloc(1, BLOCK_SECTOR_SIZE);
    block_sector_t *tmpsect_2level_1 = calloc(1, BLOCK_SECTOR_SIZE);
    int i = 0;
    if (newindex >= DIR_LEN && oldindex < DIR_LEN) {
      // 1st indir
      free_map_allocate (1, &(disk_inode->single_indir[0]));
    } 
    if (newindex >= DIR_LEN + BLOCK_SECTOR_SIZE / 4 &&
      oldindex < DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
      // 2nd indir
      free_map_allocate (1, &(disk_inode->single_indir[1]));
    }
    if (newindex >= DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {

      if (oldindex < DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
        free_map_allocate (1, &(disk_inode->double_indir));
      }

      buffer_read (fs_device, disk_inode->double_indir, tmpsect_2level_1, 0, BLOCK_SECTOR_SIZE);
      // tmpsect_2level_1 = calloc(1, BLOCK_SECTOR_SIZE);
      int newtmp_index = newindex - (DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4);
      int newtmp_index2 = newtmp_index / (BLOCK_SECTOR_SIZE / 4);

      int oldtmp_index = oldindex - (DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4);
      int oldtmp_index2 = oldtmp_index / (BLOCK_SECTOR_SIZE / 4);
      if (oldtmp_index < 0) {
        oldtmp_index2 = -1;
      }
      for (i = oldtmp_index2 + 1; i <= newtmp_index2; i++) {
        free_map_allocate (1, &(tmpsect_2level_1[i]));
      }

      buffer_write (fs_device, disk_inode->double_indir, tmpsect_2level_1, 0, BLOCK_SECTOR_SIZE);
    }

    // read the internal pages
    if (newindex >= DIR_LEN) {
      buffer_read (fs_device, disk_inode->single_indir[0], tmpsect_1st, 0, BLOCK_SECTOR_SIZE);
    }
    if (newindex >= DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
      buffer_read (fs_device, disk_inode->single_indir[1], tmpsect_2nd, 0, BLOCK_SECTOR_SIZE);
    }

    // alloc the data sector
    block_sector_t *cur_sector = calloc(1, BLOCK_SECTOR_SIZE);
    for (i = oldindex + 1; i <= newindex; i++) {
      if (i < DIR_LEN) {
        free_map_allocate (1, &(disk_inode->dir[i]));
        buffer_write (fs_device, disk_inode->dir[i], zeros, 0, BLOCK_SECTOR_SIZE);
      } else if (i < DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
        // 1st 1-level
        int index_1st_level = i - DIR_LEN;
        free_map_allocate (1, &(tmpsect_1st[index_1st_level]));
        buffer_write (fs_device, tmpsect_1st[index_1st_level] ,zeros, 0, BLOCK_SECTOR_SIZE);
      } else if (i < DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
        int index_1st_level = i - DIR_LEN - BLOCK_SECTOR_SIZE / 4;
        free_map_allocate (1, &(tmpsect_2nd[index_1st_level]));
        buffer_write (fs_device, tmpsect_2nd[index_1st_level] ,zeros, 0, BLOCK_SECTOR_SIZE);
      } else {
        // double director case
        int index_1st_level = i - DIR_LEN - 2 * BLOCK_SECTOR_SIZE / 4;
        int tmp_index = index_1st_level / (BLOCK_SECTOR_SIZE / 4);
        int tmp_index2 = index_1st_level % (BLOCK_SECTOR_SIZE / 4);
        if (tmp_index2 == 0) {
          // beginng of the 2-level page
          memset (cur_sector, 0, BLOCK_SECTOR_SIZE);
          // cur_sector = calloc(1, BLOCK_SECTOR_SIZE);
        } else {
          buffer_read (fs_device, tmpsect_2level_1[tmp_index], cur_sector, 0, BLOCK_SECTOR_SIZE);
        }
        
        free_map_allocate (1, &(cur_sector[tmp_index2]));
        buffer_write (fs_device, cur_sector[tmp_index2], zeros, 0, BLOCK_SECTOR_SIZE);
        buffer_write (fs_device, tmpsect_2level_1[tmp_index], cur_sector, 0, BLOCK_SECTOR_SIZE);
      } 
    }
    // write back the page content to the disk
    if (newindex >= DIR_LEN) {
      buffer_write (fs_device, disk_inode->single_indir[0], tmpsect_1st, 0, BLOCK_SECTOR_SIZE);
    }
    if (newindex >= DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
      buffer_write (fs_device, disk_inode->single_indir[1], tmpsect_2nd, 0, BLOCK_SECTOR_SIZE);
    }
    free (tmpsect_1st);
    free (tmpsect_2nd);
    free (tmpsect_2level_1);
    free (cur_sector);
    free (zeros);
    buffer_write (fs_device, inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  }
  lock_release (&inode->extend_lock);
}


/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{ 
  lock_acquire (&inode->length_lock);
  int len = inode->data.length;
  lock_release (&inode->length_lock);
  return len;
}
