#include "filesys/cache.h"
#include <debug.h>
#include <round.h>
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"

struct cache *memory_cache;

/* Initialize memory cache */
void cache_init(void) {
  /* Allocate a memory cache on heap for current thread/process */
  memory_cache = (struct cache*) malloc(sizeof(struct cache));
  memory_cache->clock_ptr = 0;
  lock_init(&memory_cache->l);

  int i;
  for (i = 0; i < CACHE_SIZE; i++) {
    cache_block_init(&memory_cache->blocks[i]);
  }
  memory_cache->cache_hits = 0;
  memory_cache->cache_tries = 0;
  memory_cache->disk_reads = 0;
  memory_cache->disk_writes = 0;
}

/* Initialize cache block BLOCK */
void cache_block_init(struct cache_block *block) {
  block->sector = 0;
  
  block->used = false;
  block->valid = false;
  block->dirty = false;
  
  memset(&block->data, 0, BLOCK_SECTOR_SIZE);
  lock_init(&block->l);
}


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
off_t cache_read (block_sector_t sector, void *buffer, off_t size, off_t sector_offs)
{ 
    /* Base Case */
    if (sector_offs > BLOCK_SECTOR_SIZE) {
      return 0;
    }

    int i = 0, found_cache = 0;
    struct cache_block *block;

    /* Lock the memory cache */
    lock_acquire(&memory_cache->l);
    memory_cache->cache_tries++;

    /* Check for cache in our memory */
    for (i = 0; i < CACHE_SIZE; i++) {
      block = &memory_cache->blocks[i];
      /* Note for synchronization: 
          This cache implementation ensures that a block's dirty bit 
          and sector number can only change when locks on both memory cache and 
          the block itself are held, so it allows cache replacement policy
          to be unblocking when looking for a cache block to evict */
      if (block->valid && block->sector == sector) {
          lock_acquire(&block->l);
          found_cache = 1;
          memory_cache->cache_hits++;
          break;
      }
    }

    /* If not found in cache, evict a cache block and allocate  */
    /* a new cache slot and fetch in the new cache block */
    if (!found_cache) {
      block = evict_cache();
      /* Free the memory cache so others can use it */
      lock_release(&memory_cache->l);

      block->sector = sector;

      block->valid = true;
      block->dirty = false;

      block_read (fs_device, sector, block->data);
      memory_cache->disk_reads++;
    } else {
      /* Free the memory cache so others can use it */
      lock_release(&memory_cache->l);
    }

    /* Read in the data from the cache block */
    off_t sector_left = BLOCK_SECTOR_SIZE - sector_offs;
    off_t bytes_read = sector_left > size ? size : sector_left;

    memcpy(buffer, block->data + sector_offs, bytes_read);
    block->used = true;

    /* Release lock on cache block */
    lock_release(&block->l);

    return bytes_read;
}

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
off_t cache_write (block_sector_t sector, const void *buffer, off_t size, off_t sector_offs)
{   

    /* Base Case */
    if (sector_offs > BLOCK_SECTOR_SIZE) {
      return 0;
    }

    int i = 0, found_cache = 0;
    struct cache_block *block;

    /* Lock the memory cache */
    lock_acquire(&memory_cache->l);
    memory_cache->cache_tries++;

    /* Check for cache in our memory */
    for (i = 0; i < CACHE_SIZE; i++) {
      block = &memory_cache->blocks[i];
      /* Note for synchronization: 
          This cache implementation ensures that a block's dirty bit 
          and sector number can only change when locks on both memory cache and 
          the block itself are held, so it allows cache replacement policy
          to be unblocking when looking for a cache block to evict */
      if (block->valid && block->sector == sector) {
          lock_acquire(&block->l);
          found_cache = 1;
          memory_cache->cache_hits++;
          break;
      }
    }

    /* If not found in cache, evict a cache block and allocate  */
    /* a new cache slot and fetch in the new cache block */
    if (!found_cache) {
      block = evict_cache();
      /* Free the memory cache so others can use it */
      lock_release(&memory_cache->l);

      block->sector = sector;

      block->valid = true;

      block_read (fs_device, sector, block->data);
      memory_cache->disk_reads++;
    } else {
      /* Free the memory cache so others can use it */
      lock_release(&memory_cache->l);
    }


    /* Read in the data from the cache block */
    off_t sector_left = BLOCK_SECTOR_SIZE - sector_offs;
    off_t bytes_written = sector_left > size ? size : sector_left;

    memcpy(block->data + sector_offs, buffer, bytes_written);
    block->used = true;
    block->dirty = true;
    /* Release lock on cache block */
    lock_release(&block->l);


    return bytes_written;
}


/* Evict a cache block by the clock replacement algorithm. 
   Flush any changes if the evicted cache is dirty.
   Return the cache_block that is evicted. 

   This method will return the first free cache block if not all
   cache blocks are used and allocated yet, and mark it valid.

   It will keep the lock on the cache block found to help ensure synchronization.

   This function is NOT thread-safe. Need outside synchronization. */
struct cache_block *evict_cache(void) {
    struct cache_block *block = NULL;

    while (true) {
      block = &memory_cache->blocks[memory_cache->clock_ptr];

      /* Note for synchronization: 
          If any cache block has a lock being held by a process, it implies it's valid and 
          used, so we don't evict it. We use lock_try_acquire to ensure our cache 
          replacement policy is nonblocking when looking for a block to evict. */
      int lock_acquired = lock_try_acquire(&block->l);
      if (lock_acquired) {
        if (!block->valid) {
          return block;
        } else if (block->used) {
          block->used = false;
        } else {
          if (block->dirty) {
            flush_to_disk(block);
          } 
          return block;
        }
        lock_release(&block->l);
      }
      memory_cache->clock_ptr = (memory_cache->clock_ptr + 1) % CACHE_SIZE;
    }
}


/* Flush changes in cache BLOCK to disk, if dirty. */
void flush_to_disk(struct cache_block *block) {
  block_write (fs_device, block->sector, &block->data);
  memory_cache->disk_writes++;
  block->dirty = false;
}


/* Flush all changes among all cache blocks to disk, if any. 
   Usually called on system shutdown, or a write behind cache.*/
void flush_all_cache(void) {
    int i = 0;
    struct cache_block *block;

    lock_acquire(&memory_cache->l);

    for (i = 0; i < CACHE_SIZE; i++) {
      block = &memory_cache->blocks[i];
      /*  Note we HAVE to use blocking lock acquire to ensure that any processes 
          that's still using the cache can finish, because it may write things 
          to the cahce. */
      lock_acquire(&block->l);
      if (block->dirty) {
        flush_to_disk(block);
      } 
      lock_release(&block->l);
    }

    lock_release(&memory_cache->l);
}

/* Close the cache by flushing all changes to disk and free the cache heap memory. */
void cache_close(void) {
  flush_all_cache();
  free(memory_cache);
}
