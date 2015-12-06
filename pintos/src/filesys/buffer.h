#ifndef FILESYS_BUFFER_H
#define FILESYS_BUFFER_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "threads/synch.h"
#include "devices/block.h"

struct sector 
  {
  	char content[512];
  };

struct sector_cache 
  {
  	struct list_elem cache_elem;			/* list elem to store in the sector cache list */
  	block_sector_t sector_id;		/* id of the sector */
  	struct block *block_id;			/* id of the device block */
  	struct sector *sector_location; /* location of the sector, which holds data */
    bool valid; 					/* whether this sector is valid */
    bool dirty;						/* whether this sector is dirty */
  };

/* Init the buffer system */
bool buffer_init(void);

/* Read and write */
void buffer_read (struct block *block, block_sector_t sector, void *buffer);
void buffer_write (struct block *block, block_sector_t sector, const void *buffer);



#endif /* filesys/file.h */