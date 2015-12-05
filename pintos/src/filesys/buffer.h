#ifndef FILESYS_BUFFER_H
#define FILESYS_BUFFER_H

#include "filesys/off_t.h"
#include "threads/synch.h"

struct sector 
  {
  	char[512];
  };

struct sector_cache 
  {
  	list_elem cache_elem;			/* list elem to store in the sector cache list */
  	block_sector_t sector_id;		/* id of the sector */
  	struct block *block_id;			/* id of the device block */
  	struct sector *sector_location; /* location of the sector, which holds data */
    bool valid; 					/* whether this sector is valid */
    bool dirty;						/* whether this sector is dirty */
    int pin_cnt;					/* number of the files related to this sector */
    struct lock pin_cnt_lock;		/* lock to make revise pin_cnt safely */
  };

/* Init the buffer system */
bool buffer_init(void);

/* Read and write */
void buffer_read (struct block *block, block_sector_t sector, void *buffer);
void block_write (struct block *block, block_sector_t sector, const void *buffer);



#endif /* filesys/file.h */