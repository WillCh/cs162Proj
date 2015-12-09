#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
// include my buffer h file HYC
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
    // block_sector_t start;               /* First data sector. uint32_t, id of the sector number */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t dir[DIR_LEN];
    block_sector_t single_indir[2];
    block_sector_t double_indir;
    // uint32_t unused[125];               /* Not used. */
  
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
// return the sector number of the pos
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
  // printf("inside bytosector, sector is %d ,pos is %d, len is %d\n",
  // inode->sector, pos, inode->data.length);
  if (pos < len) {
    int index = pos / BLOCK_SECTOR_SIZE;
    if (index < DIR_LEN) {
      return inode->data.dir[index];
    } else if (index < DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
      // 1 level indir case
      // read from the device
      block_sector_t *tmp_buf = calloc(1, BLOCK_SECTOR_SIZE);
      buffer_read (fs_device, inode->data.single_indir[0], tmp_buf);
      block_sector_t res = tmp_buf[index - DIR_LEN];
      free(tmp_buf);
      return res;

    } else if (index < DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
      // 2nd 1 level indir case
      block_sector_t *tmp_buf = calloc(1, BLOCK_SECTOR_SIZE);
      buffer_read (fs_device, inode->data.single_indir[1], tmp_buf);
      block_sector_t res = tmp_buf[index - DIR_LEN - BLOCK_SECTOR_SIZE / 4];
      free(tmp_buf);
      return res;

    } else { 
      // read the double indirector pointer
      block_sector_t *tmp_buf = calloc(1, BLOCK_SECTOR_SIZE);
      buffer_read (fs_device, inode->data.double_indir, tmp_buf);
      index = index - (DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4);
      int tmp_index = index / (BLOCK_SECTOR_SIZE / 4);
      int tmp_index2 = index % (BLOCK_SECTOR_SIZE / 4);

      // read the 2nd level
      block_sector_t id_2level = tmp_buf[tmp_index];
      buffer_read (fs_device, id_2level, tmp_buf);
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
  // printf("inside inode create, the len is %d the sector is %d\n",
  // length, sector);
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
      // printf("inside inode create the index is %d\n", index);
      // alloc the space for the internel pages
      if (index >= DIR_LEN) {
        // printf("inside the inode create has indir pnt\n");
        // 1st indir
        free_map_allocate (1, &(disk_inode->single_indir[0]));
        tmpsect_1st = calloc(1, BLOCK_SECTOR_SIZE);
      } 
      if (index >= DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
        // 2nd indir
        free_map_allocate (1, &(disk_inode->single_indir[1]));
        tmpsect_2nd = calloc(1, BLOCK_SECTOR_SIZE);
      }
      if (index >= DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {

        free_map_allocate (1, &(disk_inode->double_indir));
        tmpsect_2level_1 = calloc(1, BLOCK_SECTOR_SIZE);
        int tmp_index = index - (DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4);
        int tmp_index2 = tmp_index / (BLOCK_SECTOR_SIZE / 4);
        for (i = 0; i <= tmp_index2; i++) {
          free_map_allocate (1, &(tmpsect_2level_1[i]));
        }
        buffer_write (fs_device, disk_inode->double_indir, tmpsect_2level_1);
      }
      // alloc the data sector
      block_sector_t *cur_sector = calloc(1, BLOCK_SECTOR_SIZE);
      for (i = 0; i <= index; i++) {
        if (i < DIR_LEN) {
          free_map_allocate (1, &(disk_inode->dir[i]));
          buffer_write (fs_device, disk_inode->dir[i], zeros);
        } else if (i < DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
          // 1st 1-level
          int index_1st_level = i - DIR_LEN;
          free_map_allocate (1, &(tmpsect_1st[index_1st_level]));
          buffer_write (fs_device, tmpsect_1st[index_1st_level] ,zeros);
        } else if (i < DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
          int index_1st_level = i - DIR_LEN - BLOCK_SECTOR_SIZE / 4;
          free_map_allocate (1, &(tmpsect_2nd[index_1st_level]));
          buffer_write (fs_device, tmpsect_2nd[index_1st_level] ,zeros);
        } else {
          // double director case
          int index_1st_level = i - DIR_LEN - 2 * BLOCK_SECTOR_SIZE / 4;
          int tmp_index = index_1st_level / (BLOCK_SECTOR_SIZE / 4);
          int tmp_index2 = index_1st_level % (BLOCK_SECTOR_SIZE / 4);
          
          if (tmp_index2 == 0) {
            // beginng of the 2-level page
            memset (cur_sector, 0, BLOCK_SECTOR_SIZE);
            // cur_sector = calloc(1, BLOCK_SECTOR_SIZE);
          }
          
          free_map_allocate (1, &(cur_sector[tmp_index2]));
          buffer_write (fs_device, cur_sector[tmp_index2], zeros);
          if (tmp_index2 == BLOCK_SECTOR_SIZE / 4 - 1) {
            // last entry of the 2-level page
            buffer_write (fs_device, tmpsect_2level_1[tmp_index], cur_sector);
          } else if (i == index) {
            // last entry
            buffer_write (fs_device, tmpsect_2level_1[tmp_index], cur_sector);
            // NOTICE, the last page may contain useless 0s
          }
        } 
      }
      // write back the page content to the disk
      if (tmpsect_1st != NULL) {
        buffer_write (fs_device, disk_inode->single_indir[0], tmpsect_1st);
      }
      // printf("the inode pnter is: \n");
      // for (i = 0; i < DIR_LEN; i++) {
      //  printf("%d\n", disk_inode->dir[i]);
      // }
      if (tmpsect_2nd != NULL) {
        buffer_write (fs_device, disk_inode->single_indir[1], tmpsect_2nd);
      }
      free (tmpsect_1st);
      free (tmpsect_2nd);
      free (tmpsect_2level_1);
      free (cur_sector);
      // write back the inode
      buffer_write (fs_device, sector, disk_inode);
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
    // printf(" bugs in the inode open 1\n");
    return NULL;
  }
    

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  // add by haoyu
  // init the lock
  lock_init (&inode->extend_lock);
  lock_init (&inode->length_lock);
  // Haoyu Change to the buffer's api
  // block_read (fs_device, inode->sector, &inode->data);  // read disk inode from sector
  // printf("before buffer read in inode open\n");
  buffer_read (fs_device, inode->sector, &inode->data);
  // if (inode == NULL) {
  //  printf(" bugs in the inode open\n");
  // }
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
            buffer_read (fs_device, disk_inode->single_indir[0], tmpsect_1st);
            free_map_release (disk_inode->single_indir[0], 1);
          } 
          if (index >= DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
            // 2nd indir
            tmpsect_2nd = calloc(1, BLOCK_SECTOR_SIZE);
            buffer_read (fs_device, disk_inode->single_indir[1], tmpsect_2nd);
            free_map_release (disk_inode->single_indir[1], 1);
          }
          if (index >= DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
            tmpsect_2level_1 = calloc(1, BLOCK_SECTOR_SIZE);
            buffer_read (fs_device, disk_inode->double_indir, tmpsect_2level_1);
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
                // cur_sector = calloc(1, BLOCK_SECTOR_SIZE);
                buffer_read (fs_device, tmpsect_2level_1[tmp_index], cur_sector);
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
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length)); 
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

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      //printf(" inside the inode read, the sector id is %d\n", sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          // changed by Haoyu
          // block_read (fs_device, sector_idx, buffer + bytes_read);
          buffer_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          // this means that the data we read not from the 
          // beginning of the sector. we need to copy the
          // data into a buffer: bounce, then copy the data from bounce
          // to our real data
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
            // changed by Haoyu
          // block_read (fs_device, sector_idx, bounce);
          buffer_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);
  // printf("inside inode read, the size we read is %d\n", bytes_read);
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
  // printf("inside inode write, sector: %d\n", inode->sector);
  // printf("beg of inode write\n");
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
      // printf("inside the write while\n");
      /* Sector to write, starting byte offset within sector. */
      // printf(" the data len is %d\n", inode->data.length);
      // block_sector_t sector_idx = byte_to_sector (inode, offset);
      block_sector_t sector_idx = byte_to_sector_helper (inode, offset, newLen);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      // off_t inode_left = inode_length (inode) - offset;
      off_t inode_left = newLen - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          // revised by haoyu
          // block_write (fs_device, sector_idx, buffer + bytes_written);
          buffer_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            // change by haoyu
            // block_read (fs_device, sector_idx, bounce);
            buffer_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          // revised by haoyu
          // block_write (fs_device, sector_idx, bounce);
          buffer_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // update the inode length
  struct inode_disk *disk_inode = &inode->data;
  lock_acquire (&inode->length_lock);
  disk_inode->length = newLen;
  lock_release (&inode->length_lock);
  buffer_write (fs_device, inode->sector, disk_inode);
  free (bounce);
  // printf("finish of inode write\n");
  // printf("the byte wee write: %d\n", bytes_written);
  return bytes_written;
}

/* function to extend the inode */
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
    // printf("inside inode create the index is %d\n", index);
    // alloc the space for the internel pages
    if (newindex >= DIR_LEN && oldindex < DIR_LEN) {
      // printf("inside the inode create has indir pnt\n");
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

      buffer_read (fs_device, disk_inode->double_indir, tmpsect_2level_1);
      // tmpsect_2level_1 = calloc(1, BLOCK_SECTOR_SIZE);
      int newtmp_index = newindex - (DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4);
      int newtmp_index2 = newtmp_index / (BLOCK_SECTOR_SIZE / 4);

      int oldtmp_index = oldindex - (DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4);
      int oldtmp_index2 = oldtmp_index / (BLOCK_SECTOR_SIZE / 4);
      oldtmp_index2 = oldtmp_index2 > 0 ? oldtmp_index2 : 0;
      for (i = oldtmp_index2 + 1; i <= newtmp_index2; i++) {
        free_map_allocate (1, &(tmpsect_2level_1[i]));
      }
      buffer_write (fs_device, disk_inode->double_indir, tmpsect_2level_1);
    }

    // read the internal pages
    if (newindex >= DIR_LEN) {
      buffer_read (fs_device, disk_inode->single_indir[0], tmpsect_1st);
    }
    if (newindex >= DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
      buffer_read (fs_device, disk_inode->single_indir[1], tmpsect_2nd);
    }

    // alloc the data sector
    block_sector_t *cur_sector = calloc(1, BLOCK_SECTOR_SIZE);

    for (i = oldindex + 1; i <= newindex; i++) {
      if (i < DIR_LEN) {
        free_map_allocate (1, &(disk_inode->dir[i]));
        buffer_write (fs_device, disk_inode->dir[i], zeros);
      } else if (i < DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
        // 1st 1-level
        int index_1st_level = i - DIR_LEN;
        free_map_allocate (1, &(tmpsect_1st[index_1st_level]));
        buffer_write (fs_device, tmpsect_1st[index_1st_level] ,zeros);
      } else if (i < DIR_LEN + 2 * BLOCK_SECTOR_SIZE / 4) {
        int index_1st_level = i - DIR_LEN - BLOCK_SECTOR_SIZE / 4;
        free_map_allocate (1, &(tmpsect_2nd[index_1st_level]));
        buffer_write (fs_device, tmpsect_2nd[index_1st_level] ,zeros);
      } else {
        // double director case
        int index_1st_level = i - DIR_LEN - 2 * BLOCK_SECTOR_SIZE / 4;
        int tmp_index = index_1st_level / (BLOCK_SECTOR_SIZE / 4);
        int tmp_index2 = index_1st_level % (BLOCK_SECTOR_SIZE / 4);
        
        if (tmp_index2 == 0) {
          // beginng of the 2-level page
          memset (cur_sector, 0, BLOCK_SECTOR_SIZE);
          // cur_sector = calloc(1, BLOCK_SECTOR_SIZE);
        }
        
        free_map_allocate (1, &(cur_sector[tmp_index2]));
        buffer_write (fs_device, cur_sector[tmp_index2], zeros);
        if (tmp_index2 == BLOCK_SECTOR_SIZE / 4 - 1) {
          // last entry of the 2-level page
          buffer_write (fs_device, tmpsect_2level_1[tmp_index], cur_sector);
        } else if (i == newindex) {
          // last entry
          buffer_write (fs_device, tmpsect_2level_1[tmp_index], cur_sector);
          // NOTICE, the last page may contain useless 0s
        }
      } 
    }
    // write back the page content to the disk
    if (newindex >= DIR_LEN) {
      buffer_write (fs_device, disk_inode->single_indir[0], tmpsect_1st);
    }
    // printf("the inode pnter is: \n");
    // for (i = 0; i < DIR_LEN; i++) {
    //  printf("%d\n", disk_inode->dir[i]);
    // }
    if (newindex >= DIR_LEN + BLOCK_SECTOR_SIZE / 4) {
      buffer_write (fs_device, disk_inode->single_indir[1], tmpsect_2nd);
    }
    free (tmpsect_1st);
    free (tmpsect_2nd);
    free (tmpsect_2level_1);
    free (cur_sector);
    // change the inode length
    // lock_acquire (&inode->length_lock);
    // disk_inode->length = newLen;
    // lock_release (&inode->length_lock);
    // write back the inode
    buffer_write (fs_device, inode->sector, disk_inode);
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
