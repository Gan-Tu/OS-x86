#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <limits.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <stdbool.h>
#include "filesys/inode.h"
#include "filesys/cache.h"

#define READDIR_MAX_LEN 14

static void syscall_handler (struct intr_frame *);
int is_valid_vaddr(void *vaddr);
int range_is_valid(void*vaddr, int range);

int practice(int i);
void halt();
void exit(int status);
int open(const char* file);
int read(int fd, const void* buffer, unsigned size);
int write(int fd, const void* buffer, unsigned size);
void close(int fd);
bool create(const char* file, unsigned initial_size);
bool remove(const char* file);
int filesize(int fd);
void seek(int fd, unsigned position);
unsigned tell(int fd);
bool chdir (const char *dir);
bool mkdir (const char *dir);
bool isdir (int fd);
bool readdir (int fd, char *name);
int inumber (int fd);
int cache_tries (void);
int cache_hits (void);
int disk_reads (void);
int disk_writes (void);
void cache_reset (void);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  void* buffer_addr, *file_name, *cmd_line;
  uint32_t* args = ((uint32_t*) f->esp);
  // make sure esp pointer is in valid memory portion
  range_is_valid(args, 4);

  switch(args[0]){
    // Process Control Syscalls
    case SYS_PRACTICE:
      range_is_valid(args, 8);
      f->eax = practice(args[1]);
      break;
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      range_is_valid(args, 8);
      exit(args[1]); 
      break;
    case SYS_EXEC:
      range_is_valid(args, 8);
      cmd_line = (char *) args[1];
      is_valid_vaddr(cmd_line);
      f->eax = exec(cmd_line);
      break;
    case SYS_WAIT:
      range_is_valid(args, 8);
      f->eax = wait(args[1]);
      break;
    // File Operation Syscalls
    case SYS_CREATE:
      range_is_valid(args, 12);
      file_name = (void*) args[1];
      is_valid_vaddr(file_name);
      f->eax = create(args[1], args[2]);
      break;
    case SYS_REMOVE:
      range_is_valid(args, 8);
      file_name= (void*) args[1];
      is_valid_vaddr(file_name);
      f->eax = remove(args[1]);
      break;
    case SYS_OPEN:
      range_is_valid(args, 8);
      file_name = (void*) args[1];
      is_valid_vaddr(file_name);
      f->eax = open((char*)args[1]);
      break;
    case SYS_READ:
      range_is_valid(args, 16);
      buffer_addr = (void*) args[2];
      range_is_valid(buffer_addr, args[3]);
      f->eax = read(args[1], (const void*)args[2], (unsigned)args[3]);
      break;
    case SYS_WRITE:
      range_is_valid(args, 16);
      buffer_addr = (void*) args[2];
      range_is_valid(buffer_addr, args[3]);
      f->eax = write(args[1], (const void*)args[2], (unsigned)args[3]);
      break;
    case SYS_CLOSE:
      range_is_valid(args, 8);
      close(args[1]);
      break;
    case SYS_FILESIZE:
      range_is_valid(args, 8);
      f->eax = filesize(args[1]);
      break;
    case SYS_SEEK:
      range_is_valid(args, 12);
      seek(args[1], args[2]);
      break;
    case SYS_TELL:
      range_is_valid(args, 8);
      f->eax = tell(args[1]);
      break;
    // Directory Syscalls
    case SYS_CHDIR:
      range_is_valid(args, 8);
      file_name = (void*) args[1];
      is_valid_vaddr(file_name);
      f->eax = chdir((char*)args[1]);
      break;
    case SYS_MKDIR:
      range_is_valid(args, 8);
      file_name = (void*) args[1];
      is_valid_vaddr(file_name);
      f->eax = mkdir((char*)args[1]);
      break;
    case SYS_READDIR:
      range_is_valid(args, 12);
      file_name = (void*) args[2];
      is_valid_vaddr(file_name);
      f->eax = readdir(args[1], (char*) args[2]);
      break;
    case SYS_ISDIR:
      range_is_valid(args, 8);
      f->eax = isdir(args[1]);
      break;
    // Other Syscalls
    case SYS_INUMBER:
      range_is_valid(args, 8);
      f->eax = inumber(args[1]);
      break;
    case SYS_CACHETRIES:
      f->eax = cache_tries ();
      break;
    case SYS_CACHEHITS:
      f->eax = cache_hits ();
      break;
    case SYS_DISKREADS:
      f->eax = disk_reads ();
      break;
    case SYS_DISKWRITES:
      f->eax = disk_writes ();
      break;
  }
}

/* Return 1 if VADDR is a valid virtual address. 
   Otherwise, exit with -1 status code. */
int is_valid_vaddr(void *vaddr){
  if(vaddr == NULL || !is_user_vaddr(vaddr) || 
    !pagedir_get_page(thread_current()->pagedir, vaddr)){
    exit(-1);
  }
  return 1;
}

/* Return 1 if [VADDR, VADDR+range) are all valid virtual addresses. 
   Otherwise, exit with -1 status code. */
int range_is_valid(void *vaddr, int range){
  is_valid_vaddr(vaddr);
  is_valid_vaddr(vaddr + range -1);
  return 1;
}

/* Process Control Syscalls*/

int practice(int i){
  return i + 1;
}

void halt(){
    shutdown_power_off();
}

void exit(int status){
  thread_current()->data->status = status;
  printf("%s: exit(%d)\n", &thread_current ()->name, status);
  thread_exit();
}

int wait(pid_t pid) {
  return process_wait((tid_t) pid);
}

pid_t exec (const char *cmd_line) {
  struct child_data* cd;
  enum intr_level old_level;

  tid_t tid = process_execute(cmd_line);
  if (tid == TID_ERROR) {
    return -1;
  }

  old_level = intr_disable ();
  struct list_elem *e;
  for (e = list_begin (&thread_current()->children); 
       e != list_end (&thread_current()->children); 
       e = list_next (e))
  {
      cd = list_entry (e, struct child_data, elem);
      if (cd->tid == tid) {
        if (cd->load_status == 0) {
          sema_down(&cd->loaded);
        }
        if (cd->load_status == -1) {
          intr_set_level (old_level);
          return -1;
        }
        break;
      }
  }
  intr_set_level (old_level);
  return (pid_t) tid;
}

/* File Operation Syscalls */

bool create(const char* file, unsigned initial_size){

  if(file == NULL){
    return 0;
  }

  int temp = filesys_create(file, initial_size, false);

  return temp;
}

// Copied from create
bool remove(const char* file){

  if (file == NULL){
    return 0;
  }

  int temp = filesys_remove(file);

  return temp;
}

int open(const char* file){

  if(file == NULL){
    return -1; 
  }

  struct file* f = filesys_open(file);
  if(f == NULL){
    return -1;
  }

  struct fd_file_mapping* newFileBlock = (struct fd_file_mapping*) malloc(sizeof(struct fd_file_mapping));
  if (newFileBlock == NULL) {
    return -1;
  }
  newFileBlock->file = f;
  thread_current()->last_fd++;
  newFileBlock->fd = thread_current()->last_fd;
  newFileBlock->is_dir = file_isdir(f);
  
  list_push_back(&thread_current()->file_mappings, &newFileBlock->elem);

  return newFileBlock->fd;
}

int read(int fd, const void* buffer, unsigned size){
  int i = -1;

  if(fd == 1 || fd < 0 || fd > 4096){
    return i;
  }

  if(fd == 0){
    char* one_byte_increment = (char*)buffer;
    for(i=0; i<size; i++){
      one_byte_increment[i] = (char)input_getc();
    }
    return size;
  }


  struct list_elem *e;

  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end (&thread_current()->file_mappings); 
       e = list_next (e))
  {
      struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
      if(f->fd == fd){
        if (f->is_dir) {
          return -1;
        }
        int bytes_read = file_read(f->file, buffer, size);
        return bytes_read;
      }
  }

  return i;
}

int write(int fd, const void* buffer, unsigned size){
  int i=-1;
  if(fd <=0 || fd>4096){
    return i;
  }

  if(fd==1){
    putbuf(buffer, size);
    return size;
  }


  struct list_elem *e;
  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end (&thread_current()->file_mappings); 
       e = list_next(e))
  {
      struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
      if(f->fd == fd){
        if (f->is_dir) {
          return -1;
        }
        int bytes_written = file_write(f->file, buffer, size);
        return bytes_written;
      }
  }

  return i;
}

void close(int fd){
  if(fd <= 1 || fd > 4096){
    return;
  }

  struct list_elem *e;

  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end (&thread_current()->file_mappings); 
       e = list_next (e))
  {
      struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
      if(f->fd == fd){
        file_close (f->file);
        list_remove(&f->elem);
        free(f);
        break;
      }
  }

}

int filesize(int fd) {
  if (fd <= 1 || fd > 4096) {
    return -1;
  }

  
  struct list_elem *e;
  int size;

  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end(&thread_current()->file_mappings); 
       e = list_next (e)) {
    struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
    if (f->fd == fd) {
      size = file_length (f->file);
      break;
    }
  }

  return size;
}

void seek(int fd, unsigned position) {
  if (fd <= 1 || fd > 4096) {
    return -1;
  }

  
  struct list_elem *e;

  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end(&thread_current()->file_mappings); 
       e = list_next (e)) {
    struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
    if (f->fd == fd) {
      file_seek (f->file, position);
      break;
    }
  }

}

unsigned tell(int fd) {
  if (fd <= 1 || fd > 4096) {
    return -1;
  }

  
  struct list_elem *e;
  unsigned position;

  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end(&thread_current()->file_mappings); 
       e = list_next (e)) {
    struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
    if (f->fd == fd) {
      position = file_tell (f->file);
      break;
    }
  }

  return position;
}

bool chdir (const char *dir) {
  
  struct dir *cur_dir = dir_open(inode_open(thread_current()->cur_dir));
  if (cur_dir == NULL) {
    return false;
  }

  struct dir *target_dir = dir_walk_chdir(dir);
  
  if (!target_dir) {
     dir_close(cur_dir);
    return false;
  }

  thread_current()->cur_dir = inode_get_inumber(dir_get_inode(target_dir));
  
  dir_close(cur_dir);
  dir_close(target_dir);

  return true;
}

bool mkdir (const char *dir) {
  if (dir == NULL) {
    return false;
  }
  return filesys_create (dir, 0, true);
}

bool isdir (int fd) {
  if(fd <= 1 || fd > 4096){
    return false;
  }

  struct list_elem *e;

  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end (&thread_current()->file_mappings); 
       e = list_next (e))
  {
      struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
      if(f->fd == fd){
        return f->is_dir;
      }
  }

  return false;
}


bool readdir (int fd, char *name) {
  if(fd <= 1 || fd > 4096){
    return -1;
  }

  struct list_elem *e;

  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end (&thread_current()->file_mappings); 
       e = list_next (e))
  {
      struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
      if(f->fd == fd){
        if (!f->is_dir) {
          return false;
        }
        struct dir *directory = dir_open (file_get_inode(f->file));
        if (directory == NULL) {
          return false;
        }
        dir_set_position(directory, file_get_position(f->file));
        bool result =  dir_readdir(directory, name);
        file_seek(f->file, dir_get_position(directory));

        // comment this out will fail open for some weird reason
        
        // dir_close(directory);
        return result;
      }
  }

  return false;
}

int inumber (int fd) {
  if(fd <= 1 || fd > 4096){
    return -1;
  }

  struct list_elem *e;

  for (e = list_begin (&thread_current()->file_mappings); 
       e != list_end (&thread_current()->file_mappings); 
       e = list_next (e))
  {
      struct fd_file_mapping *f = list_entry (e, struct fd_file_mapping, elem);
      if(f->fd == fd){
        return inode_get_inumber(file_get_inode(f->file));
      }
  }

  return -1;
}

int
cache_tries (void)
{
  return memory_cache->cache_tries;
}

int
cache_hits (void)
{
  return memory_cache->cache_hits;
}

int
disk_reads (void)
{
  return memory_cache->disk_reads;
}

int
disk_writes (void)
{
  return memory_cache->disk_writes;
}

void
cache_reset (void)
{
  cache_close ();
  cache_init ();
}
