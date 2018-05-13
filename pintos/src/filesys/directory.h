#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);

/* Part 3 */

/* Walks the directory tree according to PATH. 
   On success, return the terminating directory (opened)
   and save the filename to FILENAME. On failure, return NULL */
struct dir *dir_walk(char *path, char **filename);

/* Walks the path given and checks if the last entry is
   a directory. If it is, open it and return it. Else,
   return NULL. */
struct dir *dir_walk_chdir(char *path);

/* Return true if directory is empty. */
bool dir_empty(struct dir *dir);

/* Return position in directory */
int dir_get_position(struct dir *dir);
/* Set position in directory */
void dir_set_position(struct dir *dir, int pos);


#endif /* filesys/directory.h */
