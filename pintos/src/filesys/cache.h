#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <stdbool.h>

#define CACHE_MAX_SIZE 64
#define CACHE_SIZE 63

struct cache_block {
    block_sector_t sector;              /* Sector number of data block's disk location. */
    
    bool dirty;                         /* True if the cache changes need to flush to disk. */
    bool used;                          /* True if the cache has recently been accessed. */
    bool valid;                         /* True if the cache is valid and is for a sector. */
    
    uint8_t data[BLOCK_SECTOR_SIZE];    /* The cached data for the data block. */
    
    struct lock l;                      /* Synchronization primitive for reader/write. */
};

struct cache {
    uint8_t clock_ptr;                      /* The current clock pointer. */
    struct cache_block blocks[CACHE_SIZE];  /* Our buffer cache will not be greater
                                              than 64 sectors in size. We allow only
                                              63 cache blocks to account for size of
                                              cache metadata and other fields. */
    struct lock l;                          /* Synchronization primitive for clock 
                                               cache replacement policy, etc. */
    int cache_hits;
    int cache_tries;                        /* Gather statistics about cache performance. */
    int disk_reads;
    int disk_writes;
};

extern struct cache *memory_cache;

/* Initialize memory cache */
void cache_init(void);

/* Initialize cache block BLOCK */
void cache_block_init(struct cache_block *block);

/* Reads SIZE bytes from disk sector SECTOR into BUFFER, starting at 
   position SECTOR_OFFS of disk sector SECTOR.

   Returns the number of bytes actually read, which may be less
   than SIZE if the end of disk sector SECTOR has reached.

   It looks for the sector SECTOR in the cache first and read from
   the cache if it exists. Otherwise, we fetch the sector SECTOR
   into the cache from disk (FS_DEVICE defined in file systems).
   It evicts and flushes any cache to cache blocks as necessary.
   Then, we read from the newly fetched cache for sector SECTOR.

   Internally synchronizes accesses to cache and block devices, 
   so external cache and per-block device locking is unneeded. */
off_t cache_read (block_sector_t sector, void *buffer, off_t size, off_t sector_offs);

/* Writes SIZE bytes to disk sector SECTOR from BUFFER, starting at 
   position SECTOR_OFFS of disk sector SECTOR.

   Returns the number of bytes actually wrote, which may be less
   than SIZE if the end of disk sector SECTOR has reached.

   It looks for the sector SECTOR in the cache first and write to
   the cache if it exists. Otherwise, we fetch the sector SECTOR
   into the cache from disk (FS_DEVICE defined in file systems).
   It evicts and flushes any cache to cache blocks as necessary.
   Then, we write to the newly fetched cache for sector SECTOR.

   Internally synchronizes accesses to cache and block devices, 
   so external cache and per-block device locking is unneeded. */
off_t cache_write (block_sector_t sector, const void *buffer, off_t size, off_t sector_offs);

/* Evict a cache block by the clock replacement algorithm. 
   Flush any changes if the evicted cache is dirty.
   Return the cache_block that is evicted. 

   This method will return the first free cache block if not all
   cache blocks are used and allocated yet, and mark it valid.

   It will keep the lock on the cache block found to help ensure synchronization.

   This function is NOT thread-safe. Need outside synchronization. */
struct cache_block *evict_cache(void);


/* Flush changes in cache BLOCK to disk, if dirty. */
void flush_to_disk(struct cache_block *block);

/* Flush all changes among all cache blocks to disk, if any.
   Usually called on system shutdown, or a write behind cache.*/
void flush_all_cache(void);

/* Close the cache by flushing all changes to disk and free the cache heap memory. */
void cache_close(void);

#endif /* filesys/cache.h */
