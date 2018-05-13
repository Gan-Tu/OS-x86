#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Pointer Counts */
#define DIRECT_CNT 123
#define INDIRECT_PTRS 128
#define DOUBLY_PTRS 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    
    // uint32_t unused[125];               /* Not used. */

    block_sector_t direct[DIRECT_CNT];         /* Direct pointer to file data blocks. */
    block_sector_t indirect;            /* Indirect pointer to an indirect inode block. */
    block_sector_t doubly_indirect;     /* Doubly indirect pointer to an indirect block. */

    bool is_dir;                        /* True if this points to a directory. */
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
    block_sector_t sector;              /* Sector number of disk location. 
                                           Also the unique identifier of this inode.*/
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */

    struct lock l;                      /* Synchronization primitive. */
    bool is_dir;                        /* True if this points to a directory. */
  };

struct indirect_disk 
  {
    block_sector_t pointers[INDIRECT_PTRS];  /* Pointers to other data or indirect inode blocks */
  };


/* Return true if the Nth data block is in direct pointers. */
bool in_direct_ptr(off_t n) {
  ASSERT (n!=0);
  return n <= DIRECT_CNT;
}

/* Return true if the Nth data block is in indirect pointer. */
bool in_indirect_ptr(off_t n) {
  return n > DIRECT_CNT && (n - DIRECT_CNT) <= INDIRECT_PTRS ;
}

/* Return true if the Nth data block is in doubly indirect pointer. */
bool in_doubly_indirect_ptr(off_t n) {
  return (n - DIRECT_CNT) > INDIRECT_PTRS && 
         (n - DIRECT_CNT - INDIRECT_PTRS) <= DOUBLY_PTRS * INDIRECT_PTRS;
}

/* Return true if the Nth data block is too big for this file system */
bool too_big(off_t n) {
  return (n - DIRECT_CNT - INDIRECT_PTRS) > DOUBLY_PTRS * INDIRECT_PTRS;
}


/* Return the index into direct pointers for Nth data block. */
off_t direct_index(off_t n) {
  ASSERT (in_direct_ptr(n));
  return n - 1; 
}

/* Return the index into indirect pointers for Nth data block. */
off_t indirect_index(off_t n) {
  ASSERT (in_indirect_ptr(n));
  return n - DIRECT_CNT - 1; 
}

/* Return the first level index into doubly indirect pointers for Nth data block. */
off_t doubly_indirect_index_1(off_t n) {
  ASSERT (in_doubly_indirect_ptr(n));
  return (n - DIRECT_CNT - INDIRECT_PTRS - 1) / INDIRECT_PTRS;
}

/* Return the second level index into doubly indirect pointers for Nth data block. */
off_t doubly_indirect_index_2(off_t n) {
  ASSERT (in_doubly_indirect_ptr(n));

  return (n - DIRECT_CNT - INDIRECT_PTRS - 1) % INDIRECT_PTRS;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns 0 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  
  struct inode_disk disk_data;
  cache_read (inode->sector, &disk_data, BLOCK_SECTOR_SIZE, 0);

  off_t sector_off = pos / BLOCK_SECTOR_SIZE;

  if (in_direct_ptr(sector_off+1)) {

    return disk_data.direct[direct_index(sector_off+1)];

  } else if (in_indirect_ptr(sector_off+1)) {
    struct indirect_disk indirect_disk_node;

    // Indirect Pointer
    cache_read (disk_data.indirect, &indirect_disk_node, BLOCK_SECTOR_SIZE, 0);
    return indirect_disk_node.pointers[indirect_index(sector_off+1)];

  } else if (in_doubly_indirect_ptr(sector_off+1)) {
    struct indirect_disk doubly_indirect_disk;
    struct indirect_disk indirect;

    cache_read (disk_data.doubly_indirect, &doubly_indirect_disk, BLOCK_SECTOR_SIZE, 0);

    off_t level1_index = doubly_indirect_index_1(sector_off+1);
    cache_read (doubly_indirect_disk.pointers[level1_index], &indirect, BLOCK_SECTOR_SIZE, 0);

    return indirect.pointers[doubly_indirect_index_2(sector_off+1)];

  }
  return 0;
}

/* Extend INODE to size LENGTH. Allocate and zero out allocated disk nodes.
   Return true upon success, or false if freemap allocate fails. */
bool extend_inode_disk(struct inode_disk *disk_data, off_t length) {

  int i, j, k, z, sectors_needed;
  size_t current_sectors, target_sectors;
  static char zeros[BLOCK_SECTOR_SIZE];

  // No allocation needed
  if (disk_data->length >= length) {
    return true;
  }

  // Calculate number of free sectors needed to grow the file
  current_sectors = bytes_to_sectors(disk_data->length);
  target_sectors = bytes_to_sectors(length);

  sectors_needed = target_sectors - current_sectors;

  if (sectors_needed <= 0) {
    disk_data->length = length;
    return true;
  }

  // Adjust for sectors needed for indoe disk nodes
  if (too_big(target_sectors)) {
    return false;
  } else if (current_sectors == 0 || in_direct_ptr(current_sectors)) {
    if (in_indirect_ptr(target_sectors)) {
      sectors_needed += 1;
    } else if (in_doubly_indirect_ptr(target_sectors)) {
      // + 1 indirect, 1 doubly indirect, index+1 second level sectors
      sectors_needed += 1 + 1 + doubly_indirect_index_1(target_sectors) + 1;
    }
  } else if (in_indirect_ptr(current_sectors)) {
    if (in_doubly_indirect_ptr(target_sectors)) {
      // + 1 indirect, 1 doubly indirect, index+1 second level sectors
      sectors_needed += 1 + doubly_indirect_index_1(target_sectors) + 1;
    }
  } else {
    size_t current_index = doubly_indirect_index_1(current_sectors);
    size_t target_index = doubly_indirect_index_1(target_sectors);
    if (target_index >= current_index) {
      sectors_needed += target_index - current_index;
    } else {
      return true;
    }
  }

  // Allocate free sectors needed to grow the file
  block_sector_t *free_sectors = NULL;
  free_sectors = (block_sector_t *) calloc(sectors_needed, sizeof(block_sector_t));
  if (free_sectors == NULL) {
    return false;
  }

  bool allocation_success = true;
  i = 0;
  while (i < sectors_needed) {
    if (!free_map_allocate(1, &free_sectors[i]) ) {
      allocation_success = false;
      break;
    }
    i++;
  }

  // Roll back if allocation failed
  if (!allocation_success) {
    i = 0;
    while (i < sectors_needed) {
      if (free_sectors[i]) {
        free_map_release(free_sectors[i], 1);
      } else {
        break;
      }
      i++;
    }
    free(free_sectors);
    return false;
  }


  sectors_needed = target_sectors - current_sectors; // Data Sectors Count Only
  i = 0;
  j = 0;

  // Direct Pointers
  while (i < DIRECT_CNT && sectors_needed > 0) {
    if (!disk_data->direct[i]) {
      disk_data->direct[i] = free_sectors[j++];
      cache_write(disk_data->direct[i], zeros, BLOCK_SECTOR_SIZE, 0);
      sectors_needed--;
    }
    i++;
  }


  // Allocate indirect disks to use later on heap to avoid stacoverflow
  struct indirect_disk *disk_node = NULL;
  disk_node = (struct indirect_disk *) calloc(1, sizeof(*disk_node));
  if (disk_node == NULL) {
    free(free_sectors);
    return false;
  }

  struct indirect_disk *disk_node2 = NULL;
  disk_node2 = (struct indirect_disk *) calloc(1, sizeof(*disk_node2));
  if (disk_node2 == NULL) {
    free(free_sectors);
    free(disk_node);
    return false;
  }

  // distinguish either cache_write or block_write later
  bool indirect_new_sector;

  // Indirect Pointers
  if (sectors_needed > 0) {
      // Allocate new indirect pointer if it doesn't exist
    if (!disk_data->indirect) {
      disk_data->indirect = free_sectors[j++];
      indirect_new_sector = true;
    } else {
      // Otherwise read the existing indirect pointer
      cache_read(disk_data->indirect, disk_node, BLOCK_SECTOR_SIZE, 0);
      indirect_new_sector = false;
    }

    // Allocate new indirect pointers and data disks as need
    i = 0;
    while (i < INDIRECT_PTRS && sectors_needed > 0) {
      if (!disk_node->pointers[i]) {
        disk_node->pointers[i] = free_sectors[j++];
        cache_write(disk_node->pointers[i], zeros, BLOCK_SECTOR_SIZE, 0);
        sectors_needed--;
      }
      i++;
    }

    // Write back indirect pointer's indirect disk node
    cache_write(disk_data->indirect, disk_node, BLOCK_SECTOR_SIZE, 0);
  }

  // Doubly Indirect Pointers
  memset(disk_node, 0, sizeof(*disk_node));

  if (sectors_needed > 0) {

    bool doubly_new_sector;

      // Allocate new doubly indirect pointer if it doesn't exist
    if (!disk_data->doubly_indirect) {
      disk_data->doubly_indirect = free_sectors[j++];
      doubly_new_sector = true;
    } else {
       // Otherwise read the existing doubly indirect pointer
      cache_read(disk_data->doubly_indirect, disk_node, BLOCK_SECTOR_SIZE, 0);
      doubly_new_sector = false;
    }

    // Calculate where to start for efficiency
    k = 0;
    if (in_doubly_indirect_ptr(current_sectors)) {
      k = doubly_indirect_index_1(current_sectors);
    }
    z = doubly_indirect_index_1(target_sectors);

    // distinguish either cache_write or block_write later
    bool level1_new_sector;

    // Fill in level one doubly indirect indicies
    while (k <= z && sectors_needed > 0) {
      
      memset(disk_node2, 0, sizeof(*disk_node2));

        // Allocate new indirect pointer if it doesn't exist
      if (!disk_node->pointers[k]) {
        disk_node->pointers[k] = free_sectors[j++];
        level1_new_sector = false;
      } else {
        // Otherwise read the existing indirect pointer
        cache_read(disk_node->pointers[k], disk_node2, BLOCK_SECTOR_SIZE, 0);
        level1_new_sector = true;
      }

      // Fill in level two doubly indirect indicies
      i = 0;
      while (i < INDIRECT_PTRS && sectors_needed > 0) {
        // Allocate new indirect pointers and data disks as need
        if (!disk_node2->pointers[i]) {
          disk_node2->pointers[i] = free_sectors[j++];
          block_write(fs_device, disk_node2->pointers[i], zeros);
          sectors_needed--;
        }
        i++;
      }

      // Flush changes
      cache_write(disk_node->pointers[k], disk_node2, BLOCK_SECTOR_SIZE, 0);

      k++;
    }

    // Flush changes
    cache_write(disk_data->doubly_indirect, disk_node, BLOCK_SECTOR_SIZE, 0);
  }

  disk_data->length = length;

  // Free heap memory
  free(free_sectors);
  free(disk_node);
  free(disk_node2);

  return true;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  cache_init();
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      // We initialize file to length 0 and grow as need
      disk_inode->length = 0;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;

      if (extend_inode_disk(disk_inode, length)) {
        block_write(fs_device, sector, disk_inode);
        success = true;
      }
      free (disk_inode);
    }
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
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->removed = false;
  inode->deny_write_cnt = 0;
  lock_init(&inode->l);

  // Read if if it's directory
  struct inode_disk *disk_data = NULL;
  disk_data = calloc (1, sizeof *disk_data);
  if (disk_data == NULL) {
    return NULL;
  }
  cache_read (inode->sector, disk_data, BLOCK_SECTOR_SIZE, 0);
  inode->is_dir = disk_data->is_dir;
  free(disk_data);

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
  return inode->sector;
}

/* Closes INODE and writes it to disk->
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

          lock_acquire(&inode->l);


          // Allocate necessary temporary structures on heap
          struct inode_disk *disk_data = NULL;
          disk_data = (struct inode_disk *) calloc(1, sizeof(*disk_data));
          ASSERT(disk_data != NULL);

          struct indirect_disk *indirect_node = NULL;
          indirect_node = (struct indirect_disk *) calloc(1, sizeof(*indirect_node));
          ASSERT(indirect_node != NULL);

          struct indirect_disk *indirect_node2 = NULL;
          indirect_node2 = (struct indirect_disk *) calloc(1, sizeof(*indirect_node2));
          ASSERT(indirect_node2 != NULL);


          cache_read (inode->sector, disk_data, BLOCK_SECTOR_SIZE, 0);

          int i, j;

          // Release free map for all direct pointers
          for (i = 0; i < DIRECT_CNT; i++) {
            if (disk_data->direct[i] == 0) {
              break;
            } else {
              free_map_release (disk_data->direct[i], 1);
            }
          }

          // Release free map for the indirect pointer
          if (disk_data->indirect != 0) {
            cache_read (disk_data->indirect, indirect_node, BLOCK_SECTOR_SIZE, 0);
            for (i = 0; i < INDIRECT_PTRS; i++) {
              if (indirect_node->pointers[i] == 0) {
                break;
              } else {
                free_map_release (indirect_node->pointers[i], 1);
              }
            }
            free_map_release (disk_data->indirect, 1);
          }

          // Release free map for the doubly indirect pointer
          if (disk_data->doubly_indirect != 0) {
            // Free indirect
            cache_read (disk_data->indirect, indirect_node, BLOCK_SECTOR_SIZE, 0);
            for (i = 0; i < INDIRECT_PTRS; i++) {
              if (indirect_node->pointers[i] == 0) {
                break;
              } else {
                free_map_release (indirect_node->pointers[i], 1);
              }
            }
            free_map_release (disk_data->indirect, 1);

            // Free doubly indirect level 1
            cache_read (disk_data->doubly_indirect, indirect_node, BLOCK_SECTOR_SIZE, 0);
            for (i = 0; i < DOUBLY_PTRS; i++) {
              if (indirect_node->pointers[i] == 0) {
                break;
              } else {
                // Free doubly indirect level 2
                cache_read (indirect_node->pointers[i], indirect_node2, BLOCK_SECTOR_SIZE, 0);
                for (j = 0; j < INDIRECT_PTRS; j++) {
                  if (indirect_node2->pointers[j] == 0) {
                    break;
                  } else {
                    free_map_release (indirect_node2->pointers[j], 1);
                  }
                }
              }
              free_map_release (indirect_node->pointers[i], 1);
            }
            free_map_release (disk_data->doubly_indirect, 1);
          }

          lock_release(&inode->l);

          free_map_release (inode->sector, 1);
          free(disk_data);
          free(indirect_node);
          free(indirect_node2);

        }

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

  if (inode_length(inode) < (offset + size)) {
    return 0;
  }


  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      lock_acquire(&inode->l);
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      lock_release(&inode->l);

      if (sector_idx == 0) {
        break;
      }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      lock_acquire(&inode->l);
      off_t inode_left = inode_length (inode) - offset;
      lock_release(&inode->l);

      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read (sector_idx, buffer + bytes_read, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

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
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;


  if (inode->deny_write_cnt) {
    return 0;
  }

  // Prevent double locking
  bool use_lock = !lock_held_by_current_thread (&inode->l);

  if (use_lock) {
    lock_acquire(&inode->l);
  }
  
  if (inode_length(inode) < (offset + size)) {
    struct inode_disk disk_data;
    cache_read (inode->sector, &disk_data, BLOCK_SECTOR_SIZE, 0);
    if (!extend_inode_disk(&disk_data, offset + size)) {
      if (use_lock) {
        lock_release(&inode->l);
      }
      return 0;
    }
    cache_write(inode->sector, &disk_data, BLOCK_SECTOR_SIZE, 0);
  }

  if (use_lock) {
    lock_release(&inode->l);
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      if (use_lock) {
        lock_acquire(&inode->l);
      }

      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (use_lock) {
        lock_release(&inode->l);
      }

      ASSERT (sector_idx != 0);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      if (use_lock) {
        lock_acquire(&inode->l);
      }

      off_t inode_left = inode_length (inode) - offset;
      
      if (use_lock) {
        lock_release(&inode->l);
      }

      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;
      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0) {
        break;
      }

      cache_write (sector_idx, buffer + bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
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
  struct inode_disk disk_data;
  cache_read (inode->sector, &disk_data, BLOCK_SECTOR_SIZE, 0);
  return disk_data.length;
}

/* Return true if inode is a directory. */
bool
inode_isdir (const struct inode *inode) {
  return inode->is_dir;
}

/* Return if an inode has been marked as removed. */
bool
inode_removed(const struct inode *inode) {
  if (!inode) {
    return true;
  }
  return inode->removed;
}

/* Return number of open_cnt for inode. */
int 
inode_open_cnt (const struct inode *inode)
{ 
  if (!inode) {
    return 0;
  }
  return inode->open_cnt;
}