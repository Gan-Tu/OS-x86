#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool is_dir);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

/* Helper Functions for Project 3 */

/* Return true if the Nth data block is in direct pointers. */
bool in_direct_ptr(off_t n);
/* Return true if the Nth data block is in indirect pointer. */
bool in_indirect_ptr(off_t n);
/* Return true if the Nth data block is in doubly indirect pointer. */
bool in_doubly_indirect_ptr(off_t n);
/* Return true if the Nth data block is too big for this file system */
bool too_big(off_t n);

/* Return the index into direct pointers for Nth data block. */
off_t direct_index(off_t n);
/* Return the index into indirect pointers for Nth data block. */
off_t indirect_index(off_t n);

/* Return the first level index into doubly indirect pointers for Nth data block. */
off_t doubly_indirect_index_1(off_t n);
/* Return the second level index into doubly indirect pointers for Nth data block. */
off_t doubly_indirect_index_2(off_t n);

/* Return true if inode is a directory. */
bool inode_isdir (const struct inode *inode);

/* Return if an inode has been marked as removed. */
bool inode_removed(const struct inode *inode);

/* Return number of open_cnt for inode. */
int inode_open_cnt (const struct inode *inode);

#endif /* filesys/inode.h */
