/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines.
 */

#include "mm_alloc.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include <stdio.h>

/* The pointer to the start of heap */
static struct metadata *start;

/* Metadata of data */
struct metadata {
    size_t size;
    bool free;
    struct metadata *prev;
    struct metadata *next;
    char data[];
};

/* Create a pointer to a new metadata allocated on heap
 * of size SIZE and inserted right after block PREV. 
 * Return NULL if failed. */
void *allocate_meta(struct metadata *prev, size_t size) {
    void *ptr = sbrk(sizeof(struct metadata) + size);
    // failed to expand mapped heap region
    if (ptr == (void *) -1) {
        return NULL;
    }
    // create and initialize a new block
    struct metadata *block = (struct metadata *) ptr;
    block->size = size;
    block->free = false;
    block->prev = prev;
    block->next = NULL;
    // zero-fill the allocated memory
    memset(block->data, 0, size);
    // set pointers for blocks
    if (prev == NULL) {
        start = block;
    } else {
        block->next = prev->next;
        prev->next  = block;
    }
    // return the pointer to the metadata of the new block
    return block;
}

/* Allocate and return a pointer to a new block of heap memory of SIZE.
 * Return NULL if size is 0 or cannot allocate new requested size. */
void *mm_malloc(size_t size) {
    // base case
    if (size == 0) {
        return NULL;
    }
    // look for a first-fit free block of sufficient size
    struct metadata *cur = start;
    struct metadata *prev = NULL;
    while (cur != NULL && !(cur->free && cur->size >= size)) {
        prev = cur;
        cur = cur->next;
    }
    // no sufficiently large free block found; allocate new block
    if (cur == NULL) {
        struct metadata *ptr = allocate_meta(prev, size);
        if (ptr == NULL) {
            return NULL;
        }
        return ptr->data;
    }
    // found a free block
    size_t residual_size = cur->size - size;
    // split the block if large enough
    if (residual_size >= sizeof(struct metadata)) {
        // pointer to the metadata of new block within the block
        struct metadata *ptr = (void *) cur + sizeof(struct metadata) + size;
        // set pointers for the metadata of new block
        ptr->prev = cur;
        ptr->next = cur->next;
        ptr->free = true;
        ptr->size = residual_size - sizeof(struct metadata);
        // set pointers for block to return
        cur->free = false;
        cur->size = size;
        if (cur->next != NULL) {
            cur->next->prev = ptr;
        }
        cur->next = ptr;
    } else {
        cur->free = false;
    }
    // return the pointer to the beginning of allocated space
    return cur->data;
}

/* Free the block memory pointed at PTR. */
void mm_free(void *ptr) {
    // base case
    if (ptr == NULL) {
        return;
    }
    // get the pointer to the metadata of the block to be freed
    // note that we walk through the chain, instead of doing 
    // pointer arithmetics to prevent invalid pointer to a block
    struct metadata *cur = start;
    while (cur != NULL && cur->data != ptr) {
        cur = cur->next;
    }
    // block not found, do nothing
    if (cur == NULL) {
        return;
    }
    // mark the block as free
    cur->free = true;
    // zero out data
    memset(cur->data, 0, cur->size);
    // if the right block is free, we advance our pointer
    if (cur->next != NULL && cur->next->free == true) {
        cur = cur->next;
    } 
    // coalesce with any left free block
    if (cur->prev != NULL && cur->prev->free == true) {
        // increase size of the left block
        cur->prev->size += cur->size + sizeof(struct metadata);
        // release the metadata for this block
        cur->prev->next = cur->next;
        if (cur->next != NULL) {
            cur->next->prev = cur->prev;
        }
        // zero out metadata
        memset(cur, 0, sizeof(struct metadata));
    }
}


/* Reallocate the block of memory at PTR to SIZE. 
 * Return the pointer to the new block of memory. */
void *mm_realloc(void *ptr, size_t size) {
    // base case
    if (ptr == NULL) {
        if (size == 0) {
            return NULL;
        } else {
            return mm_malloc(size);
        }
    } else if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    // get the pointer to the metadata of the block to be freed
    // note that we walk through the chain, instead of doing 
    // pointer arithmetics to prevent invalid pointer to a block
    struct metadata *cur = start;
    while (cur != NULL && cur->data != ptr) {
        cur = cur->next;
    }
    // block not found, do nothing
    if (cur == NULL) {
        return NULL;
    }
    // copy our entire block to stack for backup
    size_t old_data_size = cur->size;
    size_t old_block_size = cur->size + sizeof(struct metadata);
    char old_block[old_block_size];
    memcpy(old_block, cur, old_block_size);
    // free old block
    mm_free(cur->data);
    // allocate a new block
    void *new_ptr = mm_malloc(size);
    if (new_ptr == NULL) {
        // restore our entire old block
        memcpy(cur, old_block, old_block_size);
        return NULL;
    }
    // copy over content of old block;
    if (old_data_size < size) {
        size = old_data_size;
    }
    memcpy(new_ptr, (void *)old_block + sizeof(struct metadata), size);
    return new_ptr;
}

