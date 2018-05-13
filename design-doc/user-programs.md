Design Document for User Programs
============================================

## Part 1: Argument Passing

### Data Structures and Functions

We will modify the `load` function in `process.c` to parse the arguments in `char* file_name`, which is passed to the `process_execute` method, because the kernel must put the arguments for the user program on the stack first, before the user program can begin execution. Specifically, we will create local variables inside `load`: `char *argv` to store the intermediate parsed argument values, so we can later copy and push them to the user stack.

### Algorithms

The algorithm goes as follows:
* Tokenize `file_name` to null-terminated string values with `strtok_r`
* Push the values of `*argv[i]` into the stack from right to left. Decrement stack pointer `esp` by the corresponding argument string's length before we push.
* Word align the stack pointer by subtracting "the last byte of `esp` value mod 4" from the `esp` address `mod 4`, so the final byte of stack pointer `esp` address is word aligned (aka. `0 mod 4`).
* Push the null pointer sentinel to stack
* Push the addresses of each argument string `argv[i]` into the stack, from right to left. Decrement stack pointer `esp` by `4` each time.
* Push the address of `argv` into stack
* Push the value of `argc` into stack
* Push a fake `return address` into the stack. For example: 0.

For pushing values to the stack, we can use one of the two following methods:
* Assign value to `esp` pointer directly, and decrement `esp` by either the length of string values pushed (step 2 above) or 4 (for pushing addresses from step 4)
* Use pointer arithmetics and `memset`

### Synchronization

* During the argument parsing phase when we tokenize `file_name`, we use `strtok_r()` because `strtok_r()` is reentrant and thread-safe, so we don't need to worry about the thread getting interrupted and the content of `file_name` getting modified before we finish parsing.
* We don't need to worry about synchronization issues while pushing arguments and setting up the stack for the user program inside the `load` function, because even if the thread gets interrupted, the thread won't hand off control to start executing the user program, until after we finish pushing arguments to the stack. Other threads and user programs cannot modify our stack either because of memory protection in place for kernel and user programs from task 2 of the project.

### Rationale

We choose to push to the stack in `load()` as it contains the tokenized `file_name` as well as `esp` stack pointer and `save_ptr`, all arguments sufficient for completing filing up the stack.

We modify the function `load` because the user stack is configured and setup in this function. We push arguments to the user stack in `load`, instead of the `start_process` function that calls the `load` function, because it makes more sense to load arguments to stack inside the `load` function, as the function name suggests.

For performance reasons, we word align the stack pointer `esp`, before we push addresses of arguments and after pushing the values of arguments, because word-aligned accesses are faster than unaligned-accesses.

The amount of coding required for our approach is small and the idea is easy to conceptualize, and it's very flexible to accommodate additional features if need. 


## Part 2: Process Control Syscalls

### Data Structures and Functions

First, we define a new function inside `syscall.c` to assist the implementation of syscalls, so syscalls have a way to safely read and write memory in user process's virtual address space:

```
int is_valid_vaddr(void *vaddr)
```

The above function returns `0` if the memory access at `vaddr` is invalid. Otherwise, it returns 1. Note that a memory access is considered invalid in cases of null pointers, invalid pointers to unmapped memory locations, or pointers to the kernel's virtual address space.

Second, we will define and implement the following new functions in `syscall.c` to handle their corresponding syscall tasks:

- `int practice (int i)`
- `void halt (void)`
- `void exit (int status)`
- `pid_t exec (const char *cmd line)`
- `int wait (pid t pid)`

Specifically, we will also modify the `syscall_handler` method in `syscall.c` accordingly so that the `syscall_handler` will invoke relevant functions defined above to handle the syscall request, depending on the type of syscall is invoked, only after `syscall_handler` has checked for the memory validity of arguments passed by the user process. 

Note, we choose to define these syscall functions individually, instead of writing all the code inside the `syscall_handler` function, for the purpose of code style and the separation of concerns.


Third, we will define the following new structure inside `thread.h`:

```
struct child_data {
    tid_t tid;                  /* child process's tid and/or pid  */
    int status;                 /* child process's exit status */
    int load_status;            /* 1 if child loaded executables successfully; 0 
                                    if the load procedure hasn't been called; -1 if failed */
    int ref_cnt;                /* 0 if both children and parent are dead;
                                   1 if either children and parent are dead;
                                   2 if both children and parent are alive .*/
    struct semaphore loaded;    /* semaphore on whether the child has loaded executables */
    struct semaphore terminated;/* semaphore on whether the child has terminated */
    struct list_elem elem;      /* list elem used for linking in a list */
}
```

It is perfectly legal for a parent process to `wait` for child processes that have already terminated by the time the parent calls `wait`, so in order for the parent to retrieve its child's exit status or learn that the child was terminated by the kernel, we save the exit status code for the child process (identified by `tid_t`) in the `status` field. 

The field `load_status` is used to keep track of the success status of a child's attempt to load executables. The semaphore `loaded` is used to block and unblock any parent process that's waiting for the child to load executable before the parent returns from `exec` syscall. The semaphore `terminated` is used to block and unblock any parent process that's waiting for the child to terminate before the parent continues with the `wait` syscall. 

The `ref_cnt` is used to keep track so the last process to exit (either parent or child) knows when to free the memory.

Correspondingly, we remove `struct semaphore temporary` declaration and any lines that uses `temporary` semaphore from `process.c`, because we will no longer need it to achieve the naive `wait` syscall placeholder implementation, as our new `wait` implementation will use `terminated` semaphore instead. For more details, please read the design doc below.


Fourth, we will add one more typedef definition inside `syscall.h`:

```
typedef int pid_t;
```

Note that we will choose to have `pid_t` be the same as the `tid_t`, which is returned by the `thread_create` call inside `process_execute`. Since the syscalls such as `exec` requires argument and return type of `pid_t`, we need to add this typedef to support it. However, since all child processes of the kernel thread are created from `process_execute` and in Pintos each thread is in its own process, it's perfectly reasonable to use `pid_t = tid_t` to identify the child process. We simply cast the variables types between `pid_t` and `tid_t` as needed.


Fifth, we will add the following new fields to the structure definition of a thread inside `thread.h`:

```
struct list children;       /* a list of child_data for all children of this thread */
struct child_data *data;    /* a pointer to the child_data of this thread, stored in 
                               the parent process's children list, if any parent. */
```

The list `children` is used to keep track of the information about all the child processes spawned off by the current thread. This is useful and necessary, because `children` helps the current thread identify the exit status of its child processes for the  `wait` syscall. It also helps the parent process by providing it information so that it can wait until it knows whether the child process successfully loaded its executable, before returning from `exec` syscall.

The pointer `data` points to the `child_data` of this thread, if any, so when the thread loads ELF executable or exits, the thread can still persist information about this thread (such as exit status code) for the parent process to use, even in situations when the this thread exits and dies.

Lastly, we add `#include devices/shutdown.h` in `syscall.c` so the `halt` syscall can call `shutdown_power_off` method, as described later, to halt and terminate Pintos.

### Algorithms


**_Modify function `tid_t thread_create` inside `thread.c`_**

When a parent process calls `thread_create` to create a new thread, we also create a new `child_data` structure to store data for this new thread. Note that we allocate `child_data` using `malloc` to avoid potential stack overflow of the parent process. From the perspective of processes, this will store the data for the new child process that are later needed by the parent process. We initialize `tid_t` to be the `tid_t` of this new thread. We initialize `load_status` to be `0`, indicating we haven't tried to load the executables yet. We point the newly created thread's `data` field to the address of the newly created `child_data`. We also call `sema_init` with value `0` to initialize the `loaded` and `terminated` semaphores. Lastly, we add this new `child_data` to the `children` list of the parent process, which is also the calling thread.

**_Modify function `static void kill (struct intr_frame *f)` inside `exception.c`_**

When a user program receives an exception, we set the process's `status` in `child_data` to `-1`. Recall that the `child_data` of current process is pointed to by the `data` pointer, which is a newly added field in the thread's structure definition.


**_New function `int is_valid_vaddr(void *vaddr)` inside `syscall.c`_**

To check if the memory access is valid at user virtual address `vaddr`, we check to see if:

- `vaddr` is not a NULL pointer
- `vaddr` is indeed a user virtual address and does not point to kernel's virtual address space. We check this by calling `is_user_vaddr (const void *vaddr)` method from `vaddr.c`
- `vaddr` doesn't point to unmapped virtual memory. We check by calling `pagedir_get_page (uint32_t *pd, const void *uaddr)` from `pagedir.c`, where `pd` is the `pagedir` field under current running thread's struct definition and `uaddr` is `vaddr`. If the return value is non-null, `vaddr` points to valid virtual memory. Otherwise, it's invalid.

If any of the above checks fail, we terminate the user process by calling the syscall `exit(-1)`, whose implementation is discussed below.


**_Modify function `syscall_handler` inside `syscall.c`_**

Upon receiving a signal, the `syscall_handler` first calls `is_valid_vaddr` on the `esp` (or `args[0]`) to make sure the stack frame pointer points to a valid memory. We also check that `esp+1`, `esp+2` and `esp+3` are valid, in case that the memory lies close to or on the page boundary.

Then, depending on the type of signal, we check to make sure that the arguments passed by the user program to the syscall also all point to valid  memories. If all arguments are valid, we call the corresponding syscall function to handle the request. Otherwise, we terminate the process with an exit code of `-1`.

Specifically for the process system calls:

- When SYS_PRACTICE is received, we check the first argument `args[1]` for `int practice (int i)` is valid
- When SYS_HALT is received, we don't need to check for any arguments for `void halt (void)`.
- When SYS_EXIT is received, we check the first argument `args[1]` for `void exit (int status)` is valid
- When SYS_EXEC is received, we check the first argument `args[1]` for `pid_t exec (const char *cmd line)` is valid. Since `args[1]` is also a pointer to the buffer in the user address space, those memories may also be invalid, so we also check that the memory address where `args[1]` points to is also valid.
- When SYS_WAIT is received, we check the first argument `args[1]` for `int wait (pid t pid)` is valid

If any of the syscall functions have a return value, we store the return value in the `eax` register of the interrupt handler frame by setting `f->eax` specifically.



**_New function `int practice (int i)` inside `syscall.c`_**

Return the value of `i+1`

**_New function `void halt (void)` inside `syscall.c`_**

Simply call `shutdown_power_off()` (declared in `devices/shutdown.h`)


**_New function `void exit (int status)` inside `syscall.c`_**

First, we save the thread's exit code in the `status` field of current thread's `child_data` structure. Then we print out the exit status of the calling user program. Lastly, we call `thread_exit()` to terminate this thread/ child process.

**_Modify function `void process_exit (void)` inside `process.c`_**

When `thread_exit()` is called, the existing implementation implicitly calls `process_exit()`. Thus, we up the semaphore `terminated` for the calling thread in `process_exit`, so that any parent thread waiting on this child process to terminate can later resume executing. Recall `terminated` can be accessed from the `data` pointer (a newly added field to the thread structure) to the `child_data` structure. 

We delete the line `sema_up (&temporary)` since we no longer uses `temporary`.

We also loop through every `child_data` in our `children` list. For each child process, we check its `ref_cnt` field of `child_data`. If it's 1, we know the child has exited so we free the corresponding `child_data`. Otherwise, we decreament `ref_cnt` of the `child_data`. 

We check the `ref_cnt` in the `child_data`, pointed to by `data`. If it's 1, we know the parent has already exited, so we will free the `child_data` to at this point. Otherwise, we decreament the `ref_cnt` by one. 


**_Modify function `static void start_process (void *file_name_)` inside `process.c`_**

We set the `load_status` field of the calling process to `1` if load succeeded (aka. `success = true`) and `load_status = -1` otherwise, right after line `success = load (file_name, &if_.eip, &if_.esp)`.

Then, we up semaphore `loaded` for the calling thread, so that any parent thread waiting on this child process to load executables can later resume executing and return from `exec` syscall.

Recall `load_status` and `loaded` can be accessed from the `data` pointer (a newly added field to the thread structure) to the `child_data` structure.

**_New function `pid t exec (const char *cmd_line)` inside `syscall.c`_**

We call `tid_t process_execute (const char *file_name)` with `file_name = cmd_line`. Upon return from `process_execute`,  we store the returned `tid_t` value and cast it to `pid_t` type. 

We then check if the field `load_status` of `child_data` for the `pid_t` child process is non-zero. If it's zero, it means the child process hasn't tried to load executables yet, so we down the semaphore `loaded` and waits for the child process to load the executable and up the semaphore `loaded`. Upon return from the semaphore down on `loaded`, it's guaranteed that we have tried to load executables and that `load_status` will be non-zero (as shown by the implementation previously). Then, we check the `load_status` field again.

If `load_status = -1`, we know that the program cannot load or run for some reasons, so we return a `pid_t` of `-1`. Otherwise, we return the `pid_t` value returned by the `process_execute` call.

**_Modify function `int process_wait (tid_t child_tid)` inside `process.c`_**

We loop through all the `child_data` stored in the `children` list of the calling thread. When we find a `child_data` corresponding to `child_tid`, we down the semaphore `terminated`. Note semaphore down is a blocking call, so no matter whether the child process associated with `child_tid` has been terminated or not, the semaphore down will only return if the child process have terminated and have called semaphore up on the `terminated` semaphore. In other words, the semaphore down only returns to the calling thread if the thread has successfully waited for the child process to terminate. Thus, we can then remove the `child_data` element for `child_tid` from the `children` list and return the exit status of the child process by returning the field stored at `status` of `child_data`.


If there's no `child_data` for `child_tid`, it means `child_tid` is either not a direct child process of the calling process, or the process already called `wait` on this `child_tid` so that the `child_data` for this `child_tid` has already been removed from the `children` list. In either case, we return `-1` immediately.

We delete the line `sema_down (&temporary)` since we no longer uses `temporary`.


**_New function `int wait (pid_t pid)` inside `syscall.c`_**

We first cast `pid_t pid` to `tid_t child_tid`, then call `int process_wait (tid_t child_tid)`. We return the exit status code that's returned by the `int wait (pid_t pid)` function.     


### Synchronization

- When creating and initializing new `child_data` for new child processes, we do the work under the `tid_t thread_create` function in `thread.c`, instead of the `process_execute` function in `process.c`, because the new thread may be scheduled (and may even exit) before `thread_create()` returns (for example, we create a child process with higher priority than the parent process). By doing declaration and initialization of `child_data` under `thread_create`, instead of directly after `thread_create` method call in `process_execute`, we avoid the race condition that the child process may exit before it returns to the parent process to continue the `process_execute`, in which case the child thread will be unable to store its exit status in `child_data`, because it hasn't been created yet.
- When we down and up the semaphores `terminated` and `loaded`, we don't need to use a while loop because no other threads except the parent process and the child process itself has access to these fields, so no race condition synchronization issues can occur.
- A child process may have not loaded the executables yet before returning from `process_excecute`. Since the parent process cannot return from the exec until it knows whether the child process successfully loaded its executable, the semaphore `loaded` as described in the algorithms sections above help synchronize and ensure `exec` is returned only after we know the success status of `load`.
- We store `child_data` on heap, instead of inside the child process itself because a child process may terminate, exit, or die before the parent process was called `wait` on that child process. If the `child_data` was saved in the child process, the memory may be freed for the child process and we will no longer have access to those exit status etc. of the child process.
- We free `child_data` when `ref_cnt == 1`, so we can effectively avoid memory leaks, regardless either the parent of child exits first.

### Rationale

There are many alternative ways to implement the syscall functions above and achieve the effects that: (1) parent process can retrieve exit code of child processes, regardless of when child processes terminate; (2) `execv` waits for child processes to load executables before returning; (3) parent waits for child process when called `wait` on the specified child process. However, our design of using semaphores to synchronize across processes and storing exit status of child process inside parent process is definitely one of the simplest, most straight-forward and clean.

By checking the validity of passed arguments and any pointers to buffers inside user program address space in advance inside `syscall_handler` and defining `is_valid_vaddr` to check the validity of an virtual address, we can also reduce potential redundancy of code and make the relevant system functions clean with clear separation of concerns.

The amount of coding required for our approach is medium and the idea is easy to conceptualize, and it's very flexible to accommodate additional features if need. Because each file syscall is handled in a separate function call, our implementation will also keep the implementation easily readable with separation of concerns.





## Part 3: File Operation Syscalls

### Data Structures and Functions

First, we will define and implement the following new functions in `syscall.c` to handle their corresponding file system calls tasks:

- `bool create (const char *file, unsigned initial size)`
- `bool remove (const char *file)`
- `int open (const char *file)`
- `int filesize (int fd)`
- `int read (int fd, void *buffer, unsigned size)`
- `int write (int fd, const void *buffer, unsigned size)`
- `void seek (int fd, unsigned position`
- `unsigned tell (int fd)`
- `void close (int fd)`


Specifically, we will also modify the `syscall_handler` method in `syscall.c` accordingly so that the `syscall_handler` will invoke relevant functions defined above to handle the file system calls request, depending on the type of syscall is invoked, only after `syscall_handler` has checked for the memory validity of arguments passed by the user process. 

Note, we choose to define these syscall functions individually, instead of writing all the code inside the `syscall_handler` function, for the purpose of code style and the separation of concerns.


Second, since the filesystem in Pintos is not thread safe, so we cannot call multiple file system functions concurrently and will need a global lock to ensure thread safety. Thus, we add the following global variable to `syscall.h`, so only at most one filesystem operation can run at the same time:

```
    struct lock global_filelock;
```

Third, we need away to assign file descriptors sequentially. Since each process has an independent set of file descriptors, we add the following global variable declaration to `process.c`:

```
int last_fd = 1;
```

Specifically, `last_fd` refers to the last file descriptors assigned to any file. Note that we initialize `last_fd` to `1` because file descriptors of `0` and `1` are already reserved.

Fourth, since some file system calls work with file descriptors directly but the Pintos filesystem work with file structures directly, we need a way to keep track of the mapping between file descriptors and files structures. Thus, we define the following new structure inside`process.c` to store the mapping between file descriptors and files in each process:

```
struct fd_file_mapping{
    int fd;                 // the file descriptor
    struct file* file;      // the file structure object
    struct list_elem elem;  // list elem used to link the structure in a list
}
```

Since each process may need to store multiple mappings between file descriptors and file structure objects, we add a new global variable inside `process.c` to keep track of the list of all mappings between opened files and their file descriptors for the given process:

```
struct list file_mappings;
```

### Algorithms

**_Modify function `load` inside `process.c`_**

Because we cannot allow anyone modify the executables while a user program is running. More strictly speaking, we cannot allow anyone modify the executables when we we are loading its executables to memory. Thus, right after the `file = filesys_open (file_name)` statement, we call `file_deny_write` from `file.c` on `file` to prevent anyone from modifying the file content. Right after `file_close (file)` statement, we call `file_allow_write` from `file.c` on `file` to re-enable write operations on the file.

**_Modify function `void process_exit (void)` inside `process.c`_**

When a process exits, we loop through all of its current open files in `file_mappings` and close all the file descriptors for the exiting process. We also free all of its `fd_file_mapping` structures in `file_mappings` to avoid memory leaks.

**_Modify function `syscall_handler` inside `syscall.c`_**

In addition to existing implementation to check the validity of memory at from `esp` to `esp+3`, as implemented in part 2 above, we add the following checks for valid memory access on arguments passed and pointers to buffers inside user program address space:

Specifically for the file system calls:

- When SYS_CREATE is received, we check the first two arguments `args[1]` and `args[2]`for `bool create (const char *file, unsigned initial size)` are valid. Since `args[1]` is also a pointer to the buffer in the user address space, those memories may also be invalid, so we also check that the memory address where `args[1]` points to is also valid.
- When SYS_REMOVE is received, we check the first argument `args[1]` for `bool remove (const char *file)` is valid. Since `args[1]` is also a pointer to the buffer in the user address space, those memories may also be invalid, so we also check that the memory address where `args[1]` points to is also valid.
- When SYS_OPEN is received, we check the first argument `args[1]` for `int open (const char *file)` is valid. Since `args[1]` is also a pointer to the buffer in the user address space, those memories may also be invalid, so we also check that the memory address where `args[1]` points to is also valid.
- When SYS_FILESIZE is received, we check the first argument `args[1]` for `int filesize (int fd)` is valid. 
- When SYS_READ is received, we check the first three argument `args[1]`, `args[2]` and `args[3]`for `int read (int fd, void *buffer, unsigned size)` are valid. Since `args[2]` is also a pointer to the buffer in the user address space, those memories may also be invalid, so we also check that the memory address where `args[1]` points to is also valid.
- When SYS_WRITE is received, we check the first three argument `args[1]`, `args[2]` and `args[3]`for `int write (int fd, const void *buffer, unsigned size)` are valid. Since `args[2]` is also a pointer to the buffer in the user address space, those memories may also be invalid, so we check that the memory address where `args[2]` points to is also valid.
- When SYS_SEEK is received, we check the first two arguments `args[1]` and `args[2]`for `void seek (int fd, unsigned position)` are valid. 
- When SYS_TELL is received, we check the first argument `args[1]` for `unsigned tell (int fd)` is valid.
- When SYS_CLOSE is received, we check the first argument `args[1]` for `void close (int fd)` is valid.


If any of the syscall functions have a return value, we store the return value in the `eax` register of the interrupt handler frame by setting `f->eax` specifically.


**_New function `bool create (const char *file, unsigned initial_size)` inside `syscall.c`_**

Acquire the `global_filelock`, and then call `bool filesys_create(const char *name, off_t initial_size)` from `filesys.c` directly with `name=file` and `unsigned initial_size = off_t initial_size`. Release the `global_filelock` and return the returned boolean value.


**_New function `bool remove (const char *file)` inside `syscall.c`_**

Acquire the `global_filelock`, and then call `bool filesys_remove (const char *name)` from `filesys.c` directly with `name=file`. Release the `global_filelock` and return the returned boolean value.

**_New function `int open (const char *file)` inside `syscall.c`_**

Acquire the `global_filelock`, and then call `struct file *filesys_open (const char *name)` from `filesys.c` directly with `name=file`. Since the filesystem function returns a file object but the file syscall requires us to return a file descriptor, we increment `last_fd` to get a new file descriptor (`last_fd+1`). We create a new `fd_file_mapping` to store the mapping between returned `file` object to the new file descriptor (`last_fd+1`). Note that we create this structure on heap using `malloc` to avoid potential stack overflow when too many files are open. We add this mapping to the `file_mappings` list of the current process. Then we release the `global_filelock` and return the new file descriptor `last_fd+1`.

**_New function `int filesize (int fd)` inside `syscall.c`_**

Acquire the `global_filelock`, and obtain the file object `file` corresponding to file descriptor `fd` by looping through the `file_mappings` list. Then we call `off_t file_length (struct file *file)` from `file.c` directly on `file`. Release the `global_filelock` and return the returned file size.


**_New function `int read (int fd, void *buffer, unsigned size)` inside `syscall.c`_**

Acquire the `global_filelock`.

If `fd=0`, we call `uint8_t input_getc (void)` from `input.c` to read from the keyboard and store the results in `buffer`. We read until we have read `size` number of characters. If an error occurred, we return `-1`.

Otherwise, we obtain the file object `file` corresponding to file descriptor `fd` by looping through the `file_mappings` list. Then we call `off_t file_read (struct file *file, void *buffer, off_t size)` from `file.c` directly on `file`, `buffer` and `off_t size = unsigned size`. 

Release the `global_filelock` and return the number of bytes actually read.

**_New function `int write (int fd, const void *buffer, unsigned size)` inside `syscall.c`_**

Acquire the `global_filelock`.

If `fd=1`, we call `void putbuf (const char *buffer, size_t n)` from `console.c` with `buffer` and `n=size` to  write all of buffer to console. We return the actual number written, or 0 if no bytes could be written at all.

Otherwise, we obtain the file object `file` corresponding to file descriptor `fd` by looping through the `file_mappings` list. Then we call `off_t file_write (struct file *file, void *buffer, off_t size)` from `file.c` directly on `file`, `buffer` and `off_t size = unsigned size`. 

Release the `global_filelock` and return the number of bytes actually written.

**_New function `void seek (int fd, unsigned position)` inside `syscall.c`_**

Acquire the `global_filelock`, and obtain the file object `file` corresponding to file descriptor `fd` by looping through the `file_mappings` list. Then we call `void file_seek (struct file *, off_t)` from `file.c` directly on `file` and `position = off_t`. Release the `global_filelock`.

**_New function `unsigned tell (int fd)` inside `syscall.c`_**

Acquire the `global_filelock`, and obtain the file object `file` corresponding to file descriptor `fd` by looping through the `file_mappings` list. Then we call `off_t file_tell (struct file *file)` from `file.c` directly on `file`. Release the `global_filelock` and returns the position of the next byte to be read or written.

**_New function `void close (int fd)` inside `syscall.c`_**

Acquire the `global_filelock`, and obtain the file object `file` corresponding to file descriptor `fd` by looping through the `file_mappings` list. Then we call `void file_close (struct file *)` from `file.c` directly on `file`. We then remove the mapping between the file descriptors `fd` and the `file` from the `file_mappings` list of the current process and free the relevant memory. Lastly, Release the `global_filelock`.


### Synchronization

Since the filesystem in Pintos is not thread safe, we cannot call multiple file system functions concurrently. Since we also have a list of mappings between file descriptors and file objects, we want to avoid concurrent access and modification to the list. Thus, to avoid potential race conditions, we always acquire a global lock before any list operation or executing any file system calls on Pintos filesystem. We release the global lock if and only if we have finished the file system call and finished any list operation on `file_mappings`. 


### Rationale

Most of the functionalities are already implemented by Pintos filesystem, so most of our file system calls just directly call the appropriate functions. However, since we also work with file descriptors for file system calls, which the Pintos file system don't support, it makes sense for us to store a mapping between the files and file descriptors.

We choose to store the mapping between file descriptors and the files directly inside `process.c`, instead of the alternative solution of storing the file descriptor `fd` inside the file structure. This is because a file can have multiple file descriptors, if the file is either opened multiple times in a single process or opened by different processes. Some of these file descriptors can even have the same `fd` value because each process assign the file descriptors independently. Thus, instead of storing a list of `fd`s and its owner processes for each file descriptor opened for a given `file` structure, it's much simpler to manage and maintain if each process just store all the file descriptors it has and its corresponding files. The memory complexity between both approaches are roughly O(n) though, where n is the number of files opened.

Another advantage of storing a mapping between the file descriptors and the files in each process over the alternative approach of storing these information directly under file structures is that some file system calls only pass in file descriptors `fd`s. To work with them, we need to know which files these `fd`s refer to. In our design, the lookup time is simply O(n), where n is the number of open files for the given process. But in alternative design, it requires us to save a list of all files open across processes; it requires us to loop through all files in order to find the file for which it has the same `fd` and has the owner process be the calling process. The time complexity of alternative design will be O(N), where N will be the number of _all_ files open across _all_ processes, instead of the files open only for the given process.

Thus, our design is more efficient and easy to maintain than alternative designs. The amount of coding required for our approach is medium and the idea is easy to conceptualize, and it's also very flexible to accommodate additional features if need.


## Design Document Additional Questions
### 1. Invalid stack pointer ($esp) testcase

A test case that uses an invalid stack pointer is **sc-bad-sp.c**. This test works by syscalling after setting the stack pointer to something invalid. On line 18 of **sc-bad-sp.c**, `asm volatile` is used to write inline assembly that can't be reordered by the compiler. `movl $.-(64*1024*1024), %esp` moves the stack pointer to an invalid value (64 MB below the code segment) then traps into the kernel with `int 0x30` to start the system call.

### 2. Valid stack pointer at page boundary test case

A test case that passes a valid stack pointer at a page boundary is **sc-bad-arg.c**. This test puts a syscall number in the highest available space in user memory, then makes a syscall with the stack pointer at that address. This fails because the syscall argument is out of user memory, which is not allowed access. On line 14 of **sc-bad-sc.c**, `asm volatile` is used to write inline assembly that can't be reordered by the compiler. `movl` is used to first move the stack pointer to the top of memory (`movl` into register `%%esp`) then to load the syscall number into that location of memory (`movl` into the memory pointed to by `%esp`). `int 0x30` then traps into the kernel and starts the syscall.

### 3. Not fully tested project requirements

A part of the project that is not fully tested by the test suite is Part 3. Specifically, not all syscalls that the project spec mandates we implement are called in the test code. `remove`, `filesize`, and `tell` don't appear anywhere in the test suite. A test for this part of the project could proceed as follows: create a file, write some data to it, call filesize and check that it matches the amount of data written to the file, then seek to the end of the file and call tell, checking that it also is equal to the amount of data written to the file. Finally, call remove on that file and check that the file no longer exists.

## GDB Questions
### 1. Breakpoint at process_execute

The name of the thread that is running is `main` and its address is `0xc000e000`.  

There is one other thread present in pintos at this time and that is the `idle` thread:
	

```
{tid = 1, status = THREAD_RUNNING, name = "main", '\000' <repeats 11 times>, stack = 0xc000ee0c "\210", <incomplete sequence \357>, priority = 31, allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067},
	
{tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem = {prev = 0xc000e020, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}

```

### 2. Backtrace

Backtrace of the current thread:
```
process_execute (file_name=file_name@entry=0xc0007d50 "args-none") at ../../userprog/process.c:32
0xc002025e in run_task (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:288
0xc00208e4 in run_actions (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:340
main () at ../../threads/init.c:133
```

The line of c code corresponding to the `process_execute` function call:

```
process_wait (process_execute (task));
```

The line of c code corresponding to the `run_task` function call:

```
a->function (argv);
```

The line of c code corresponding to the `run_actions` function call:

```
run_actions (argv);
```

### 3. Breakpoint at start_process

The name of the thread that is running is `args-none` and its address is `0xc010a000`.  `main` and `idle` threads are still present as other threads. 

```
0xc000e000 {tid = 1, status = THREAD_BLOCKED, name = "main", '\000' <repeats 11 times>, stack = 0xc000eebc "\001", priority = 31, allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020}, elem = {prev = 0xc0036554 <temporary+4>, next = 0xc003655c <temporary+12>}, pagedir = 0x0, magic = 3446325067}

0xc0104000 {tid = 2, status = THREAD_BLOCKED, name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem = {prev = 0xc000e020, next = 0xc010a020}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}

0xc010a000 {tid = 3, status = THREAD_RUNNING, name = "args-none\000\000\000\000\000\000", stack = 0xc010afd4 "", priority = 31, allelem = {prev = 0xc0104020, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}
```



### 4. Location of creation of the thread running start_process

The thread running `start_process` is created on `line 45` of `process.c`:

```
tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
```

### 5. Pagefault (hex)

The line that caused the page fault is `0x0804870c`

### 6. Pagefault (symbols loaded)

```
_start (argc=<error reading variable: can't compute CFA for this frame>, argv=<error reading variable: can't compute CFA for this frame>) at ../../lib/user/entry.c:9
```

### 7. Reason of Pagefault

The reason our user program page faulted on this line is because of error reading in the argc and argv variables. The error given by the debugger is that `can't compute CFA for this frame`. 



