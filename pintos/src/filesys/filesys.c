#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

#define READDIR_MAX_LEN 14

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
  cache_close();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir)
{
  block_sector_t inode_sector = 0;

  char *path = name;
  char *filename = NULL;
  struct dir *dir = dir_walk(path, &filename);

  bool success;
  if (is_dir) {
    success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 2) 
                  && dir_add (dir, filename, inode_sector));
    if (success) {
      // Initialize two entries
      struct dir *newDir = dir_open(inode_open(inode_sector));
      char curr_name[2] = ".\0";
      char parent_name[3] = "..\0";
      success = (newDir != NULL 
          && dir_add (newDir, curr_name, inode_sector) 
          && dir_add (newDir, parent_name, inode_get_inumber(dir_get_inode(dir))));
      dir_close(newDir);
    }
  } else {
    success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));
  }

  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);

  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct inode *inode = NULL;

  if(strcmp(name, "/")==0){
    return file_open(inode_open(ROOT_DIR_SECTOR));
  }
  if (strlen(name) <= 0) {
    return NULL;
  }

  char *filename = NULL;
  struct dir *dir = dir_walk(name, &filename);
 
  if (dir != NULL) {
    if (strlen(filename) > 0) {
      dir_lookup (dir, filename, &inode);
    } else {
      inode = dir_get_inode( dir );
    }
    dir_close (dir);
  }

  free(filename);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  if (strcmp("/", name) == 0)
    return false;

  char *filename = NULL;
  if (strcmp("/", name) == 0){
    return false;
  }
  struct dir *dir = dir_walk(name, &filename);

  struct inode* child = NULL;
  dir_lookup (dir, filename, &child);
  if(child == NULL){
    dir_close(dir);
    return false;
  }
  bool a = (dir != NULL 
            && inode_get_inumber(child) != ROOT_DIR_SECTOR
            && inode_get_inumber(child) != thread_current()->cur_dir);
  bool success = false; 

  if(inode_isdir(child)){
    struct dir *child_dir = dir_open(child);
    success = (a
                && dir_empty(child_dir)
                && inode_open_cnt(child) <= 4 // no idea why it's not 1. prob some bug somewhere, but works
                && dir_remove (dir,  filename));
    dir_close (child_dir);
  } else {
    success = (a && dir_remove(dir, filename));
  }

  dir_close (dir);

  free(filename);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
