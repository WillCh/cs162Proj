#include "filesys/buffer.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

/* List of open sectors */
static struct list open_sectors;
// here we assume the head of list the recent one,
// the tail of the list is the oldest one

struct lock list_revise_lock;

/* pointer to the sector memory */
struct sector *sector_entry;

/* init the buffer system */
bool
buffer_init(void)
{
  // init the sector head list
  list_init (&open_sectors);
  sector_entry = NULL;
  // calloc 64 sector space as buffer
  sector_entry = calloc (64, sizeof *sector_entry);
  if (sector_entry == NULL) {
    return false;
  }
  // init the meta data 
  struct sector_cache *cache = NULL;
  cache = calloc (64, sizeof *cache);
  int i = 0;
  struct sector_cache *iter = NULL;
  if (cache == NULL) {
    free (sector_entry);
    return false;
  }
  for (i; i < 64; i++) {
    iter = cache + i;
    iter->valid = false;
    lock_init (&(iter->pin_cnt_lock));
    list_push_front (&open_sectors, iter->cache_elem);
  }
  lock_init (&list_revise_lock);
  return true;
}

/* Reads sector SECTOR from BLOCK into BUFFER, which must
   have room for BLOCK_SECTOR_SIZE bytes.
   Internally synchronizes accesses to block devices, so external
   per-block device locking is unneeded. */
void
buffer_read (struct block *block, block_sector_t sector, void *buffer)
{
  // first check whether there is a sector_cache which has the same
  // block_sector_t, starting from head of the list
  struct list_elem *e;
  // since when we iterate the list, another thread may revise its
  // structure.... so here I only allow one thread to use this list
  // per time
  lock_acquire (&list_revise_lock);
  bool finished = false;
  for (e = list_begin (&open_sectors); e != list_end (&open_sectors);
       e = list_next (e))
    {
      struct sector_cache *cache_entry = list_entry 
          (e, struct sector_cache, cache_elem);
      if (cache_entry->valid) {
        if (cache_entry->sector_id == sector && 
          cache_entry->block_id == block) {
          // we find a match
          // set the pin_cnt
          // since we only allow one thread to use the list
          // then we may NOT need the pin_cnt lock...
          // we may even do not need the pin_cnt!!
          // copy the content to the buffer
          memcpy (buffer, cache_entry->sector_location, sizeof *cache_entry);
          // put the sector to the head of the list
          list_remove (e);
          list_push_front (&open_sectors, e);
          finished = true;
          break;
        }
      } else {
        // if we meet a invalid one, means we have serached 
        // to the end of valid sector
        break; 
      }
    }
  if (!finished) {
    // need to read from the real device and insert to the responding block
    if (e == list_end (&open_sectors)) {
      e = list_pop_back (&open_sectors);
    }
    struct sector_cache *cache_entry = list_entry 
          (e, struct sector_cache, cache_elem);
    // read the data from device to the sector
    block_read (block, sector, cache_entry->sector_location);
    // copy the data to the buffer
    memcpy (buffer, cache_entry->sector_location, sizeof *cache_entry);
    // put the elem to the front of the list
    list_remove (e);
    list_push_front (&open_sectors, e);
  }
  lock_release (&list_revise_lock);
}