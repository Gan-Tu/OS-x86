Design Document for Threads
======================================

## Part 1: Efficient Alarm Clock

### Data Structures

- We add a global variable called `struct list sleeping_threads` in `timer.c` to keep track of the list of sleeping threads, in the ascending order of when the thread should wake up timeline wise.
- We add the field `wakeup_tick` to the structure definition of `thread` in `thread.h` to keep track of when each thread should wake up.
- We create a `list_less_func` function for comparing wakeup ticker values of threads, as required by the linked list `list.h` implementation.

### Algorithms

**_In `timer_sleep` function under `timer.c`:_**

- If the number of ticks we need to sleep for is zero or negative (`TICKS <= 0`), we return immediately because there is no need to sleep.
- Otherwise, we turn off the interrupts temporarily as we enter a critical section below:
	- We calculate the wakeup ticker value for the thread by adding the `TICKS` argument to the global tick (timer ticks since OS booted).
	- We update the `wakeup_tick` field of this thread to store the time at when it should wake up.
	- We add this thread to the list of `sleeping_threads` using `list_insert_ordered`, linking on the `thread->elem` filed. Note that we make sure to maintain the sorted order of the list by the ascending order of wakeup ticker value, so the threads that need to wake up first will be in the front of the `sleeping_threads` list.
	- We block the thread.
- We turn the interrupts back on again.


**_In `timer_interrupt` under `timer.c`:_**

- Increase global ticker value and call `thread_tick()` to record thread statistics as usual.
- We turn off the interrupts temporarily as we enter a critical section below:
	- While the `sleeping_threads` list is non-empty **and** the global ticker value is larger or equal to the wakeup ticker value of the front thread of the list: (1) remove the front element from the list; (2) unblock the thread that element associates to.
- We turn the interrupts back on again.

### Synchronization

**_Synchronization for `timer_sleep`:_**

- If interrupts happen while we are checking the value of `TICKS` before we turn off the interrupts for the critical section, the thread will end up sleeping longer than `TICKS` timer clicks since the time when the thread calls `timer_sleep`, due to a possible incremented global time tick value later when the thread runs again. However, it doesn't corrupt our algorithm because the function still ensures that the thread sleeps **at least** `TICKS` time clicks.
- We turn off interrupts when calculating wakeup ticker value because if interrupts occur immediately after we called `thread_ticks()` to get global ticker value before we add `TICKS` to get the wakeup ticker, we may end up having a wake up ticker value that is already smaller than the global ticker value by the time the `timer_sleep` resumes. This will be undesirable because we will unnecessarily add threads that technically have already slept past the intended wakeup time to the `sleeping_threads` list, consuming unnecessarily memories at a risk of stack overflow, and making our algorithm later in `timer_interrupt` attempt to wake up these threads even at a later time.
- We turn off interrupts when adding the thread to the list of sleeping thread and blocking the threads, because these operations involve modifying a list and changing the thread status, which of both need to be thread-safe.

**_Synchronization for `timer_interrupt`:_**

- Comparing wakeup ticker value against global ticker value and involves reading a list, which needs to be thread-safe. Thus, we turn off interrupts.
- Removing the element from `sleeping_threads` and unblock the thread if it's time for it to wake up involves modifying a list and changing a thread's status, which of both need to be thread-safe. Thus, we turn off interrupts.

### Rationale

- Our design is better than alternative approach of busy waiting because our approach does not consume unnecessary computation and resources.
- Our design always keeps a sorted list of threads based on sleeping thread's wakeup ticker time. It's more time efficient than alternative approaches of sorting the list each time because sorting takes O(NlogN) time while keeping a list sorted at the time of insertion is only O(N) time.
- Our design keeps the list sorted so the timer interrupt can be run as fast as possible, thus more time efficient comparing to alternative approaches of looping through all sleeping threads to find out ones that need to be waken up. In average case, our algorithm is O(1) while alternative approach is O(N) time.
- Our design has a space complexity of O(N) in terms of the number of threads that are sleeping. This is inevitable because we need to know when each thread needs to wake up.
- The amount of coding required for our approach is small and the idea is easy to conceptualize, and it's very flexible to accommodate additional features if need. 

## Part 2: Priority Scheduler

### Data Structures

In order to implement priority donation, we need to modify the thread structure to track more data. 

We will add the following fields to the thread structure:

 - `int effective_priority`: the thread's current effective priority (in contrast to the `priority` filed, which tracks the base priority of a thread)
 - `lock *lock_waiting`: the lock that the thread is waiting on, if any
 - `struct list *locks_holding`: a list of the locks that the current thread is holding, if any.
    - In order to store a linked list of locks, we add a `list_elem elem` to the `struct lock` in `sync.h`, as the required list element used by the `list.h` API.

### Algorithms


**Main Priority Scheduler Logic**

_In `next_thread_to_run` function under `thread.c`_

- The scheduler scans the ready queue (`ready_list`) and picks the thread with the highest effective priority to run. If multiple threads have the same highest effective priority, we break tie using 'round robin' order.

_In `sema_up` and `cond_signal` functions under `sync.c`:_

- Semaphores and condition variables scan their list of waiting threads (`waiters`), and pick the thread with the highest effective priority to acquire the resource next. If multiple threads have the same highest effective priority, we break tie using 'round robin' order.
- Note since lock is a special case of semaphore, and that `lock_release` calls `sema_up` for scheduling, we do not need to scan the list of waiters for the lock, as it is taken care of by  both the changes we made in `sema_up` and the priority donation scheme, which is to be described below.

**Priority Donation**

We perform priority donations to tackle the priority inversion problem. The idea of priority donation is that when a high-priority thread (A) has to wait to acquire a lock, which is already held by a lower-priority thread (B), we temporarily raise B’s effective priority to A’s effective priority.

_These changes will be made in `lock_acquire` functions under `sync.c`.:_

- When a thread has to wait for a lock, we point the `lock_waiting` field of thread A to that lock.
- When a thread (A) attempts to acquire a lock and finds that the lock is already held by another thread (B). If B has a higher or equal effective priority than A, A will wait for the lock to be released as normal. However, if B has a lower effective priority than A, we set the lock holder's effective priority (B) to A's effective priority. If B is also waiting on another lock, we follow the `lock_waiting` pointer iteratively to walk up the chain until we either reach a thread that is not waiting on any locks, or reach a thread that has a higher or equal effective priority than thread A. We set the effective priority of all the threads in the middle to also be the effective priority of thread A.
- Once a thread successfully acquires a lock, we add the lock to the `locks_holding` field of this thread in order to keep track of all the locks the thread is currently holding.

_These changes will be made in `lock_release` functions under `sync.c`.:_

- When a thread releases a lock, we remove the lock from the `locks_holding` field of this thread in order to keep track of all the locks the thread is currently holding.
- When a thread releases a lock, it checks if the thread still holds other locks, and if any of the waiting thread's effective priority is higher than the thread's own base priority. If yes, the thread sets its new effective priority to be the max of the effective priorities among all waiters of the locks that the thread is still holding. Otherwise, it should reset its effective priority to be its original base priority.

_The following illustrates the correctness of our algorithms under a Nested Priority Case_:

```
       D ---- 
      (M2)   |
	     v
 A --> B --> C
(H)   (M)   (L)
```

In above case, a high priority thread (A) waits for a medium priority thread (B), which in turn waits for a lower priority thread (C). Another medium priority thread (D), with a higher priority than B but still lower than A, also waits for C. Because we want A to run first, A needs to donate priority to C in order to unblock C. However, we also want to run B before D, so as to unblock and B and run A. But if we only donate priority to C and not raising B's priority, C will give the resource to D when it releases the lock. This is not good. To prevent that, we thus also need to set the effective priority of B to A as well when we donate priority of A to C.
 

**Priority Changes**

- By default, we initialize the effective priority of a thread to be the same as its base priority under `init_thread` function.
- If a thread has a decreased effective priority after calling `thread_set_priority` or after `lock_release`, we check to see if the thread still holds the highest effective priority among all the ready threads. If yes, it continues to execute without yielding to cpu. Otherwise, the thread has to yield to the cpu.

### Synchronization

- The function that calls `next_thread_to_run`, `schedule`, always has interrupts turned off, so there's no race conditions for synchronization concerns when the scheduler scans the list of ready threads.
- To prevent race conditions on changing the values of `effective_priority`, `lock_waiting`, and `locks_holding` fields when performing priority donation or releasing a lock, we turn off interrupts.

### Rationale

- We choose to scan the list of ready threads each time when we pick a new thread to run, because it's possible for threads to change their priority using `thread_set_priority`, or when acquiring and releasing locks. We can't guarantee that the list will stay in sorted order just by inserting in sorted order at the time of insertion. Therefore, instead of reordering the list each time we update the effective priority of any thread, which can be O(N) time each time, our approach is more time efficient because it only takes O(N) time once by scanning the entire list of ready list.
- It makes the most sense for priority donation to occur in the functions `lock_acquire` and `lock_release` because those are the only times that a thread can change its effective priority, besides calling `thread_set_priority`. By doing so, we also move the work of checking the need for priority donation outside the `next_thread_to_run` function, thus reducing repetitive work and speeding up the scheduler.
- Our design takes roughly O(N+M) space, where N is the number of threads and M is the number of locks, because we only added 3 more fields to each thread and each lock can only held by one thread at a time.
- The amount of coding required for our approach is small and the idea is easy to conceptualize, and it's very flexible to accommodate additional features if need. 

## Part 3: Multi-Level Feedback Queue Scheduler (MLFQS)

### Data Structures

We can use the same ready list that we've used for other parts of the project, and simply find the thread with the maximum priority. This will enforce the round robin running structure for threads with the same calculated priority because we add to the back of the ready list and select the first max element we come across.
```
struct list priority_list;
```

- We will also add the following fields to the thread structure:
  - `int nice`: thread's current nice value
  - `fixed_point_t recent_cpu`: thread's most recently calculated recent_cpu value.
- We will add a global variable `fixed_point_t load_avg` to track the system's most recently calculated load average value.


### Algorithms


**Priority Calculation**

_These changes are made in `thread_tick` function under `thread.c`_

- Whenever a timer interrupt occurs, we increment the `recent_cpu` of the running thread by 1: `recent_cpu = recent_cpu+1`.
- Once every second (`timer_ticks() % TIMER_FREQ == 0`), we need to recalculate the `recent_cpu` and `load_avg` values. Thus, we run the calculations below inside timer interrupt for **all** threads:
  - We first calculate the value of `ready_threads` by taking `size(ready_list) + 1` to get the number of threads that are either running (1) or ready to run (`size(ready_list)`) at time of update (not including the idle thread).
  - Then, we calculate new `load_avg` using `ready_threads` through formula below:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<img src="https://latex.codecogs.com/gif.latex?\text{load\_avg}&space;=&space;\frac{59}{60}\cdot\text{load\_avg}&space;&plus;&space;\frac{1}{60}\cdot\text{ready\_threads}" title="\text{load\_avg} = \frac{59}{60}\cdot\text{load\_avg} + \frac{1}{60}\cdot\text{ready\_threads}" />. 

  - Next, we calculate new `recent_cpu` with using running thread's `nice` value and new `load_avg` through formula below:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<img src="https://latex.codecogs.com/gif.latex?\text{recent\_cpu}&space;=&space;\frac{(2\cdot\text{load\_avg})}{(2\cdot\text{load\_avg}&plus;1)}&space;\cdot&space;\text{recent\_cpu}&space;&plus;&space;\text{nice}" title="\text{recent\_cpu} = \frac{(2\cdot\text{load\_avg})}{(2\cdot\text{load\_avg}+1)} \cdot \text{recent\_cpu} + \text{nice}"/>

- Once every fourth clock tick, we run the calculations `priority = PRI_MAX − (recent_cpu/4) − (nice × 2)` for **all threads** inside timer interrupt to get the updated priority values for every thread. Then, we process and move ready threads to the new corresponding ready queue among the 64 priority ready queues based on their new priority. _Important:_, we will move the threads based on their original order in each ready queue, so as to maintain the right round robin order in their new ready queue.


**_Scheduler Algorithm_**
- We run the same algorithm as above to determine which thread to get the resource next for locks, semaphores, and condition variables.

**_Priority Modification_**
- We ignore the priority argument to `thread_create()` and `init_thread()`, as well as any calls to `thread_set_priority()`. The initial priority value of a thread and the value returned by `thread_get_priority()` both only return the priority value calculated based on its nice value and the MLFQS algorithm.
- When `void thread_set_nice (int new_nice)` is called, we set the thread's `nice` value to the `new_nice` value and recomputes the priority value for the running thread. If the running thread no longer has the highest priority, we move the running thread to the new corresponding priority ready queue, and yield the CPU.

**_Priority Donation_**
- In `lock_acquire` and `lock_release`, we will no longer perform tasks like the priority donation, reset of priority values, etc. as implemented in part 2, if we are using MLFS algorithm.

**Other Functions**
- Since variables such as `load_avg` and `recent_cpu` are calculated every second in the timer interrupt, functions like `int thread_get_load_avg()`, `int thread_get_recent_cpu()` will simply return these corresponding `load_avg` and/or `recent_cpu` value, multiplied by 100 as function header states.
- Since `nice` values are set and saved in the threads, `int thread_get_nice (void)` will also just return the corresponding `nice` value.


### Synchronization

We turn off interrupts when calculating priority values, `recent_cpu`, and `load_avg`, and when moving threads to their new priority ready queues in order to prevent modifications to these shared data under race conditions inside the timer interrupt.


### Rationale


- One short coming of the current algorithm is that all the calculations to update `recent_cpu`, `load_avg`, and priority values have to be done in the timer interrupts. These operations from MLFS are computationally expensive in nature, so it will slow down the timer interrupt handler as we have more threads concurrently running. Specifically, the time complexity is O(N) each time, where N is the number of threads in the system, because we need to update every single thread. One risk of doing so is missing clock ticks and such, when the number of threads get too big. However, considering the calculations for `recent_cpu` and `load_avg` are done once per second, and that priority values are only updated once four clock clicks, the amortized performance should still be reasonable quick.
- The amount of coding required for our approach is small and the idea is easy to conceptualize, and it's very flexible to accommodate additional features if need. 

## Additional Questions

### 1.) A Single Bug
A test case that would reveal this bug proceeds as follows:
```
(inside thread A, with priority 30)
s1, s2 = semaphore(0)
create thread B, priority = 10
down s1
create thread D, p = 50
yield
create thread C, p = 20
```
The threads B, C, D should run the following code
```
thread B {
    acquire L1
    up s1
    down s2
    print "Hello from B!"
    up s2
    release L1
}

thread C {
    down s2
    print "Hello from C!"
    up s2
}

thread D {
    acquire L1
    release L1
}
```
The expected output should be
```
Hello from B!
Hello from C!
```
because thread D should donate its priority to B upon trying to acquire the lock. However, the actual output from this test case will be
```
Hello from C!
Hello from B!
```
because C has a larger base priority than B, so the buggy `sema_up` will release to C first when both C and B are waiting.

### 2.) MLFQS Table
timer ticks | R(A) | R(B) | R(C) | P(A) | P(B) | P(C) | thread to run
------------|------|------|------|------|------|------|--------------
 0          |  0   |  0   |  0   | 63   | 61   | 59   | A
 4          |  4   |  0   |  0   | 62   | 61   | 59   | A
 8          |  8   |  0   |  0   | 61   | 61   | 59   | B
12          |  8   |  4   |  0   | 61   | 60   | 59   | A
16          | 12   |  4   |  0   | 60   | 60   | 59   | B
20          | 12   |  8   |  0   | 60   | 59   | 59   | A
24          | 16   |  8   |  0   | 59   | 59   | 59   | C
28          | 16   |  8   |  4   | 59   | 59   | 58   | B
32          | 16   | 12   |  4   | 59   | 58   | 58   | A
36          | 20   | 12   |  4   | 58   | 58   | 58   | C

### 3.) Ambiguities in the scheduler doc?
The ambiguity in the scheduler doc is that, given a tie in priority, which thread should run first? The doc says that the threads should run in the round-robin style, but it doesn't say where this cycle should start. In calculating the values above, we used the rule that priority lists are queues. Due to the First-In-First-Out nature of the queues, the cycle should start with the thread that was pushed to that particular priority queue first. For example, at timer tick 8, we choose thread B because it had a priority of 61 before thread A did.

