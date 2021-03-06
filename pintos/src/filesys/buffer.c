#include "filesys/buffer.h"
#include <debug.h>
#include <list.h>

#include "threads/malloc.h"


/* List of open sectors */
static struct list open_sectors;
// here we assume the head of list the recent one,
// the tail of the list is the oldest one

struct lock list_revise_lock;

/* pointer to the sector memory */
struct sector *sector_entry;

static int num_access;
static int num_hits;

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
    iter->dirty = false;
    iter->sector_location = sector_entry + i;
    list_push_front (&open_sectors, &(iter->cache_elem));
  }
  lock_init (&list_revise_lock);
  num_access = 0;
  num_hits = 0;
  return true;
}

/* write back all the dirty sectors */
void 
buffer_update_disk ()
{
  struct list_elem *e;
  
  lock_acquire (&list_revise_lock);
  for (e = list_begin (&open_sectors); e != list_end (&open_sectors);
       e = list_next (e))
    {
      struct sector_cache *cache_entry = list_entry 
          (e, struct sector_cache, cache_elem);
      if (cache_entry->valid && cache_entry->dirty) {
        block_write(cache_entry->block_id, cache_entry->sector_id,
         cache_entry->sector_location);
        cache_entry->dirty = false;
      }
    }
  lock_release (&list_revise_lock);
}

/* Reads sector SECTOR from BLOCK into BUFFER, which must
   have room for BLOCK_SECTOR_SIZE bytes.
   Internally synchronizes accesses to block devices, so external
   per-block device locking is unneeded. */
void
buffer_read (struct block *block, block_sector_t sector, void *buffer, off_t offset, off_t size)
{
  // first check whether there is a sector_cache which has the same
  // block_sector_t, starting from head of the list
  struct list_elem *e;
  // since when we iterate the list, another thread may revise its
  // structure.... so here I only allow one thread to use this list
  // per time
  lock_acquire (&list_revise_lock);
  num_access += 1;
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
          char *entry_point = (char *) (cache_entry->sector_location) + offset;
          memcpy (buffer, entry_point, size);
          // put the sector to the head of the list
          list_remove (e);
          list_push_front (&open_sectors, e);
          finished = true;
          num_hits += 1;
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
    } else {
      list_remove (e);
    }
    struct sector_cache *cache_entry = list_entry 
          (e, struct sector_cache, cache_elem);
    // if it's dirty, we write it back
    if (cache_entry->dirty && cache_entry->valid) {
      block_write(cache_entry->block_id, cache_entry->sector_id,
         cache_entry->sector_location);
    }
    // read the data from device to the sector
    block_read (block, sector, (void*)(cache_entry->sector_location));
    cache_entry->valid = true;
    cache_entry->dirty = false;
    cache_entry->block_id = block;
    cache_entry->sector_id = sector;
    // copy the data to the buffer
    char *entry_point = (char *) (cache_entry->sector_location) + offset;
    memcpy (buffer, entry_point, size);
    // put the elem to the front of the list
    list_push_front (&open_sectors, e);
  }
  lock_release (&list_revise_lock);
}

/* write the data to the device, the data will start
 * start from the offset of the device's sector, and the size
 * of the data we want to write is size. The data is
 * in the buffer.
 */
void 
buffer_write (struct block *block, block_sector_t sector, const void *buffer, off_t offset, off_t size)
{
  // first to iterate the list to see whether the block is here
  struct list_elem *e;
  // since when we iterate the list, another thread may revise its
  // structure.... so here I only allow one thread to use this list
  // per time
  lock_acquire (&list_revise_lock);
  num_access += 1;
  bool finished = false;
  for (e = list_begin (&open_sectors); e != list_end (&open_sectors);
       e = list_next (e))
    {
      struct sector_cache *cache_entry = list_entry 
          (e, struct sector_cache, cache_elem);
      if (cache_entry->valid) {
        if (cache_entry->sector_id == sector && 
          cache_entry->block_id == block) {
          char *entry_point = (char *) (cache_entry->sector_location) + offset;
          memcpy (entry_point, buffer, size);
          // put the sector to the head of the list
          list_remove (e);
          list_push_front (&open_sectors, e);
          finished = true;
          cache_entry->valid = true;
          cache_entry->dirty = true;
          num_hits += 1;
          break;
        }
      } else {
        // we have searched to the end of the valid list
        break;
      }
    }

  if (!finished) {
    // printf("write to the disk\n");
    // need to read from the real device and insert to the responding block
    if (e == list_end (&open_sectors)) {
      e = list_pop_back (&open_sectors);
    } else {
      list_remove (e);
    }
    struct sector_cache *cache_entry = list_entry 
          (e, struct sector_cache, cache_elem);
    // if it's dirty, we write it back
    if (cache_entry->dirty && cache_entry->valid) {
      block_write(cache_entry->block_id, cache_entry->sector_id,
         cache_entry->sector_location);
    }

    // then write the data to this buffer
    char *entry_point = (char *) (cache_entry->sector_location) + offset;
    memcpy (entry_point, buffer, size);
    cache_entry->valid = true;
    cache_entry->dirty = true;
    cache_entry->block_id = block;
    cache_entry->sector_id = sector;
    // put the elem to the front of the list
    list_push_front (&open_sectors, e);
  }
  lock_release (&list_revise_lock);
}

/* helper function for our test, which
 * record the hit of our buffer
 */
void buffer_performance(int *access, int *hit){
  *access = num_access;
  *hit = num_hits;
  num_access = 0;
  num_hits = 0;
}

/* helper function for our test, which
 * clean our buffer system.
 */
void buffer_clean(void)
{
  struct list_elem *e;
  buffer_update_disk();
  lock_acquire (&list_revise_lock);
  for (e = list_begin (&open_sectors); e != list_end (&open_sectors);
       e = list_next (e))
    {
      struct sector_cache *cache_entry = list_entry 
          (e, struct sector_cache, cache_elem);
      cache_entry->valid = false;
    }
  num_access = 0;
  num_hits = 0;
  lock_release (&list_revise_lock);
}
