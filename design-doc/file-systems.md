Design Document for File Systems
============================================

## Part 1: Buffer Cache

### Data Structures and Functions

We define a new data structure called `cache_block` to hold the cache for any file block, with necessary information needed by the clock replacement policy:

```
struct cache_block {
    block_sector_t sector;              /* Sector number of data block's disk location. */
    
    bool dirty;                         /* True if the cache changes need to flush to disk. */
    bool used;                          /* True if the cache has recently been accessed. */
    bool valid;                         /* True if the cache is valid and is for a sector. */
    
    uint8_t data[BLOCK_SECTOR_SIZE];    /* The cached data for the data block. */
    
    struct lock l;                      /* Synchronization primitive for reader/write. */
}
```

To represent the entire buffer cache and enable the clock cache replacement policy, we define the following data structure:

```
struct cache {
    uint8_t clock_ptr;                  /* The current clock pointer. */
    struct cache_block blocks[63];      /* Our buffer cache will not be greater
                                           than 64 sectors in size. We allow only
                                           63 cache blocks to account for size of
                                           cache metadata and other fields. */
    struct lock l;                      /* Synchronization primitive for clock 
                                           cache replacement policy, etc. */
}
```

### Algorithms


Whenever `inode_read_at()` or `inode_write_at()` is called:

- We check to see if the `inode_disk` for the given `inode` is already in our cache, by comparing the `sector` number of the passed `inode` with all the cache blocks.
    - If `inode_disk` for the given `inode` does not already exits in cache, we go and fetch it, evicting any necessary cache blocks according to the clock cache replacement policy and writing any unwritten data to disk if the dirty bit is set.
- Then, we continue to check if the file blocks that need to be read or written are in the cache, by first computing the `sector` numbers for those to-be-read or to-be-modified file blocks and then compare them with cache blocks.
    - If not, we fetch those blocks to cache, evicting any necessary cache blocks according to the clock cache replacement policy and writing any unwritten data to disk if the dirty bit is set.
- Then, if `inode_read_at()` is called, we read from the corresponding cache. Otherwise, we write to the corresponding cache block and set the dirty bit. For both, we synchronize using the reader-writer synchronization scheme and prioritize writers to readers.


For our buffer cache, we choose the 2-chance clock cache replacement policy where we replace a block if the used bit is 0; otherwise we reset the used bit and advance the clock pointer, until we find a block to evict. Initially when the cache blocks are not all filled, all blocks will have valid bit set to invalid so we fill up the cache blocks first before any eviction. 

For right now, we do _not_ support the write-behind cache, so we flush unwritten data to disk only if either (1) a cache block is dirty and is evicted from the cache, or (2) the system shuts down, in which case we will flush all unwritten data to disk in `filesys_done()`.

We will also delete the "bounce buffer" so we copy data into and out of sectors in the buffer cache directly.

_Updates after Design Review:_

We will implement `cache_read` and `cache_write` to replace all calls to `block_read` and `block_write`. The main logic still remain the same as above. In this way, we will allow for a clear separation between task1 and task2/3 and make merging the two halves much cleaner.


### Synchronization

We hold a lock on the main buffer cache whenever we look for a certain disk sector in our buffer cache. We hold the lock for both the main buffer cache and the cache block itself when we are evicting that cache block. We release the lock on main buffer cache when we have found the corresponding cache block, or have evicted a cache block, but we still hold the lock on the found/evicted cache block itself, so we can allow other processes to read the buffer cache while having exclusive access to the cache block for read/write/fetching from disk operations. We release the lock on the cache block only after we have finished our operations on that cache block.

We also ensures that a block's dirty bit and sector number can only change when locks on both memory cache and the block itself are held, so it allows cache replacement policy to be unblocking when looking for a cache block to evict.

To prevent evicting a cache block while it is being read or written, we evict the cache block if and only if no processes hold a lock on that cache block yet (so it's not being used). We accomplish this using `lock_try_acquire`.

Note, this implementation doesn't use reader/writer solution so we don't allow asynchronous read yet.

### Rationale

Using the reader and writer pattern for the cache allows for the most parallelism possible because multiple parallel reads from a cache will not conflict with each other. If we choose to simply have a lock on the cache block, it will unnecessarily slow down our processes. 

A write-back cache is used because instead of spending most of the time writing to a disk, when lots of writes to the same block is used, we only flush out changes when a cache is evicted or the system shuts down. In this way, we also speed up our processes significantly.

We choose to not evict a cache block or reset the used bit for a cache block if it has any active or waiting readers or writers, during a clock replacement policy, because having active or waiting readers or writers for a cache block intuitively means that this cache will be used soon. Therefore, instead of evicting a cache block right away or using up its second chance for clock replacement policy, keeping the cache block alive is similar to the idea of the optimal MIN cache replacement policy. It also avoids doing more could-have-avoided disk IOs to fetch those disks again, when we already know that these cache will be used again soon. Thus, our algorithm will do slightly better than the pure clock replacement policy in a multi-threaded situation.

Also, using a clock replacement policy is also performant and easy to implement. 

## Part 2: Extensible files

### Data Structures and Functions

We need to update the `inode_disk` structure to have the following signature:

```
struct inode_disk {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */

    block_sector_t direct[123];         /* Direct pointer to file data blocks. */
    block_sector_t indirect;            /* Indirect pointer to an indirect inode block. */
    block_sector_t doubly_indirect;     /* Doubly indirect pointer to an indirect block. */

    /* ... 1 more block_sector_t definition used for part 3 */
}
```

We choose to have 123 direct pointers so that the `inode_disk` is exactly `BLOCK_SECTOR_SIZE = 512` bytes in size, based on size calculations. We zero out any pointer that is not used.

Due to the limit on the cache size, we also need to update the `inode` structure to no longer have a copy of the `inode_disk` stored inside it. Instead, we store only the `sector` number of the `inode_disk` inside `inode` as `sector`, which will also acts as the unique identifier for the `inode` to be returned by the `int inumber (int fd)` syscall in part 3. We will also have a lock for synchronization purposes.

```
struct inode {
    /* ... original inode definitions except inode_disk  */

    struct lock l;                      /* Synchronization primitive. */

    /* ... more definitions used for part 3 */
}
```

To maximize acceptable file sizes, the direct pointers in `inode_disk` points directly to a file block containing file data on disk, while the indirect and doubly indirect pointers point to a data block on disk consisting of additional pointers. Specifically, we define an `indirect` disk block to be as follows:

```
struct indirect_disk {
    block_sector_t pointers[128];  /* Pointers to other data or indirect inode blocks */
}
```

Note we have 128 pointers so that the indirect block on disk is also exactly `BLOCK_SECTOR_SIZE = 512` bytes in size. Thus, for any given `inode_disk`, it can at maximum store file sizes of around `123 * 512 + 128 * 512 + 128 * 128 * 512 ~ 8 MB`.

Similarly, we zero out any pointers that are not used. Whether these pointers point to another `indirect_disk` or data block depends on whether it's pointed to by an `indirect` or `doubly_indirect` pointer from `inode_disk`. 

### Algorithms

Whenever we read or write a file, we first find its corresponding `inode` and then its corresponding `inode_disk` based on `inode`'s `sector` number. We fetch them from the disk if it's not already in the cache. 

To read an `inode` starting at `offset` for a length of `size` (using `inode_read_at`), we first find the starting sector number of the block on disk for which the data at `offset` is at. We do so by first computing its sector offset: `offset % BLOCK_SECTOR_SIZE`. Then, we fetch its corresponding sector number from the direct pointers, the indirect pointer, and the doubly indirect pointer, depending on its offset. We will modify `byte_to_sector` function to help us efficiently obtain the disk sector number depending on the file offset. Note that to fetch the sector number from indirect pointer and doubly indirect pointer means we will have to walk the tree linked by these pointers, fetching indirect blocks on disk as necessary. 

Once we find the initial data sector at `offset`, we keep reading until we finished reading `size` amount of data or reach the end of file (EOF), which is indicated by the `offset` at which we are at is at the `length` of the file. Whenever we finish reading data from one block, we will simply fetch the next file data block from disk based on the direct pointers, the indirect pointer, and the doubly indirect pointer.

To write a file starting at `offset` for a length of `size` (using `inode_write_at`), the file size may or may not need to grow. If it doesn't need to grow, the algorithm is basically the the same as read above, but instead of read we do write. 

However, if the file size will grow, we implement **file growth** as follows:

- We keep writing the file until we have reached the original end of the file (EOF), which is indicated by the current position is past the `length` of the original file. 
- Then, we keep writing until the current file block on disk is full.
- _Loop until_ we finish writing `size` amount, or all pointers in `inode` are full and we reach the maximum allowed file size:
  - We allocate a new file data block on disk.
  - We link this new file data block up in the first available direct pointer, or either in indirect pointer or the doubly indirect pointer of file's `inode` depending on which ones are still first available. We create new intermediate `indirect_disk` blocks on disk as necessary. 
  - We write new content to the newly allocated file data block on disk until we finished writing `size` amount, or this block is full.
- We update the `length` of file and `inode` accordingly to reflect the new file size.

There's one _edge_ case: if we start writing from an `offset` that is beyond the EOF (`offset > length`), which can occur due to some file seeks past the end of the file, we choose to first zero-fill the file content between the original EOF to the starting `offset` initially for simplicity, before start writing the file.

_Updates after Design Review:_

File extension must be an atomic action and thus, we will rollback any blocks that may have been allocated, if any errors come up doing the middle of the execution, such as the free map fails to allocate another block.

Similarly, when we close an inode, we will also "shrink" the file and free all the allocated sectors associated with the file. We will base on algorithm from the solutions from our [discussion worksheet](https://cs162.eecs.berkeley.edu/static/sections/section12-solutions.pdf). 

Finally, `free_map_allocate` and `free_map_release` are not thread-safe and thus, so we will introduce some synchronization in `freemap.c` to fix this problem.

### Synchronization

We need to ensure that 2 operations acting on different disk sectors, different files, and different directories can run simultaneously. We will use a different synchronization scheme to ensure this so we remove the `global_filelock` that we declared for naive synchronization in project 2.

We acquire the lock for the `inode` when we perform operations on it directly such as updating the length of the file, reading or updating `block_sector_t` pointers to blocks on disks. However, we release the lock on `inode` immediately after we have read relevant data or made these changes to allow different operations on different block sectors under the same `inode`.

Note that since all the files internally call `inode` operations to perform relevant actions first, the synchronizations in `inode` will thus effectively synchronize different operations on same files. Alternatively, we can conservatively add a lock to the file structure, so we always acquire a lock before accessing any field of the file structure, but release it before we call any inode functions, or reach the end of the function.

Since all read and write operations are done to the cache of the disk blocks, where disk IOs such as block writes and reads are only performed during cache fetch and cache eviction, the synchronization on different block sectors are thus effectively taken care of by the synchronization on the buffer cache from part 1.

Since `free_map_allocate` and `free_map_release` are not thread-safe and thus, we will introduce some synchronization in `freemap.c` to fix this problem.


### Rationale

Our design is mainly an implementation of the UNIX file system scheme using direct, indirect, and doubly indirect pointers, because most of the files stored on OS are relatively small files and only few files are actually large. The UNIX file system can help us make the data storage scheme efficient, as the existing use and experience of UNIX file system has shown.


## Part 3: Subdirectories

### Data Structures and Functions

```
struct inode_disk {
    /* ... more definitions used for part 2 */

    bool is_dir;                        /* True if this points to a directory. */
}
```

To persistently know if a file is an ordinary file or a directory, we need to store and indicate the `inode_disk` point to a directory or a ordinary file, so we can effectively interpret the data contents it points to. We do this by adding the `bool is_dir` field entry to the `inode_disk` definition.

To effectively walk up the directory tree when encountering relative path containing `..` or `.`, we initially create two files in each directory created. We set the `block_sector_t` for `.` to be the `block_sector_t` of itself and we set the `block_sector_t` for `..` to be the `block_sector_t` of its parent directory under which it is created. For root directory, this is itself again.

```
struct inode {
    /* ... original inode definitions except inode_disk */
    /* ... more definitions used for part 2 */

    bool is_dir;                        /* True if this points to a directory. */
}
```

We will also add the `is_dir` field to the `inode` for faster `is_dir` check, instead of having to always fetch the entire `inode_disk` each time we perform these actions. In this design, we will simply need to fetch it once upon a file or directory creation/open. 

Because directory are created using `dir_create` and files are created using `filesys_create`, which of both call `inode_create`, we thus change the function signatures of `inode_create` to take a boolean indicating whether the `inode` points to a directory or not, so the `inode_create` can correctly populate the fields for both `inode` and `inode_disk`.

We will also add a boolean `is_dir` in the `fd_file_mapping` definition to indicate whether a certain file descriptor `fd` points to a file or directory as well. We can fetch this information from `is_dir` of the `inode`. This differentiation helps us quickly decide which functions to call based solely on the file descriptor number.


```
struct thread {
    /* ... other thread definitions */
    #ifdef FILESYS
      block_sector_t *curdir;    /* The block_sector_t for the current directory of this process. */
    #endif
};
```

We add a pointer field in `thread` definition so it knows the current directory to the process it is in. 


### Algorithms


**_File Name Resolution_**

We will use the `get_next_part` function, provided in the spec, to parse any passed file name, which is potentially a relative path, to get its name parts. We will then walk the directory tree to find the final file the file name points to.

- If the file indicated by name part is not in the last resolved directory so far, we error, unless we have reached the end of string.
- If the name part corresponds to a directory, we walk into that directory
- If the name part points to a file but we have not reached the end of path name string: in other worlds, the next call to `get_next_part` is not end of string, we error. Otherwise, the file name is what we found
- We return NULL if anything errors. Otherwise, we return the resolved token
- Note we error out whenever a file name exists the 14-character limit.

**_File System_**

We need to modify functions such `filesys_open`, `filesys_create` and `filesys_remove` such that we are able to resolve the file name before passing the name to relevant functions. We will also modify these functions so it doesn't always work under the root directory, but work under the current directory of the process instead, which is saved in the `thread` as defined above.

We will also add the following new file syscalls:
 
**_New file syscall `bool chdir (const char *dir)` inside `syscall.c`_**

We perform a similar algorithm as the file name resolution described above, but instead of returning the file name, we are actually changing the `curdir` current directory pointer of the current process along the way. If we error out in the middle of the process by trying to step into a file or walking into a non-existent directory, or we reach the end of the director name string but the last token was a file, we return false and reset the `curdir` pointer to its original state before calling the function. Otherwise, we return true.


**_New file syscall `bool mkdir (const char *dir)` inside `syscall.c`_**

We perform a similar algorithm as `chdir` syscall above, but we will never change the current directory `curdir` pointer; instead, we will operate on a separate local variable directory pointer. We return false if anything fails, such as an intermediate file directory does not exist or we try to walk into a file.

Once we reach the _second to last_ valid directory name token, we check if the last token name represented in the file path already exists in the directory of _second to last_ valid directory name token. If so, we return false as either a file or directory of same name exists. Otherwise, we allocate a new disk block at `block_sector_t` for the directory content; create a new directory by calling `dir_create` in `directory.c`. Then, we call `dir_add` to add the new directory at `block_sector_t` to the new directory's parent directory (aka. the directory of _second to last_ valid directory name token); and we return true upon success.


We will also initialize two files in the new directory created. We set the `block_sector_t` for `.` to be the `block_sector_t` of itself and we set the `block_sector_t` for `..` to be the `block_sector_t` of its parent directory under which it is created. For root directory, this is itself again.


**_New file syscall `bool isdir (int fd)` inside `syscall.c`_**

Return true if the `is_dir` in the `fd_file_mapping` for file descriptor `fd` is set to true. Otherwise, return false.

**_New file syscall `bool readdir (int fd, char *name)` inside `syscall.c`_**

Return false if the `is_dir` in the `fd_file_mapping` for file descriptor `fd` is set to false, because can not read a directory that is not a directory.

Otherwise, we get the directory `dir` represented by the `inode`, and then call `bool dir_readdir (struct dir *dir, char name[NAME_MAX + 1])` from `directory.h` with this `dir` and passed `name` pointer to read the directory. We return false if it fails. 

**_New file syscall `int inumber (int fd)` inside `syscall.c`_** 

We return the sector number for the `inode_disk` of the File's `inode`. The sector number of the `inode_disk` serves as the unique identifier because each `inode` only has one `inode_disk` associated with it, which only resides in uniquely identified sectors on disks.

**_Other Changes_**

At startup, we set the file system root as the initial process's current directory. When one process starts another with the `exec` system call, the child process inherits its parent’s a copy of its current directory, so after that the two processes can maintain their current directories independently. We achieve these by modifying the `thread_create` function in `thread.c`.


### Synchronization

According to spec, _if the directory changes while it is open, then it is acceptable for some entries not to be read at all or to be read multiple times. Otherwise, each directory entry should be read once, in any order_. Thus, we only need to really think about synchronization issues that raises when multiple threads modify the same directory. 

Since directories that are write operations or change directory fields call `inode` operations directly, and since `inode` operations are already handled with synchronization primitives, we don't need to add additional locks for directory. 

Alternatively, we can conservatively add a lock to the directory structure, so we always acquire a lock before accessing any field of the directory structure, but release it before we call any inode functions, or reach the end of the function.

### Rationale

We choose to add `.` and `..` as the first two entries of that directory. This means that when resolving paths, we do not need to do anything special for these two cases as they can be treated as simple string comparison. It also helps us avoid resolving the absolute path for a file from the root directory, because it will involve a significant amount more unnecessary and repetitive disk IOs to read in the directory. 

Note since our cache size is limited, storing the directory blocks in our cache to avoid repetitive walks from the root directory is also not ideal, because it involves taking up unnecessary cache and causing more eviction of file cache blocks, resulting in potentially more cache misses in a long run. This is especially true when the absolute path are very long and involve lots of nested sub-directories.

Having synchronizations all handled on the cache level and `inode` level simplifies the synchronizations for directories. 


## Design Document Additional Questions


### 1. Implementation strategy for write-behind cache

Implementing a write-behind cache would involve using a forked process to periodically flush any unwritten data in the cache to the disk as well as unsetting the dirty bit for the flushed data.  This could be implemented using a non-busy timer_sleep from project 1 in a while loop.  The process managing the write-behind functionality would continuously flush any unwritten data from the cache, unsetting the dirty bit for each piece of unwritten data, and then call timer_sleep. Upon waking up the process would repeat the above steps until the system shuts down or crashes.

### 2. Implementation strategy for read-ahead cache

Implementing a read-ahead cache requires fetching the first block of the file and then forking an additional process to fetch the subsequent blocks asynchronously so the calling syscall can return immediately without blocking and waiting for the subsequent blocks to be fetched.  In the implementation, the amount of subsequent blocks to be fetched is a design choice left to the developer.  If the amount of subsequent blocks fetched is close to the size of the cache, this will result in a large amount of eviction, so that the cache can hold these subsequent blocks.  Fetching such a large amount of subsequent blocks would be beneficial for sequential reads, but would likely cause performance issues for random accesses.
