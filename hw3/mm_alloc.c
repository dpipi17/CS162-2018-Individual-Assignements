/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines.
 */

#include "mm_alloc.h"
#include <stdlib.h>
#include <stdbool.h> 
#include <string.h>
#include <unistd.h>

struct meta_data * get_free_space(size_t size);

struct meta_data {
   struct meta_data * next;
   struct meta_data * prev;
   size_t size;
   size_t unused_space_size;
   bool free;
   char chunk[0];
};

struct meta_data * head = NULL;

void *mm_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    struct meta_data * new_elem_meta_data = get_free_space(size);
    if (new_elem_meta_data == NULL) {
        return NULL;
    }
    memset(new_elem_meta_data->chunk, 0, size);

    return (void*)new_elem_meta_data->chunk;
}

void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return mm_malloc(size);
    }

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    char temp_buffer[size];
    memcpy(temp_buffer, ptr, size);

    mm_free(ptr);
    void * new_addr = mm_malloc(size);
    if (new_addr == NULL) {
        return NULL;
    }
    memcpy(new_addr, temp_buffer, size);
    
    return new_addr;
}

void mm_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct meta_data * curr = head;
    while (curr != NULL) {
        if (memcmp(curr->chunk, ptr, curr->size) == 0) {
            curr->free = true;
            curr->size += curr->unused_space_size;
            curr->unused_space_size = 0;
            break;
        }

        curr = curr->next;
    }

    if (curr->prev != NULL && curr->prev->free) {
        curr->prev->size += curr->size + sizeof(struct meta_data);

        curr->prev->next = curr->next;
        if (curr->next != NULL) {
            curr->next->prev = curr->prev;
        }
        curr = curr->prev;
    } 
    
    if (curr->next != NULL && curr->next->free) {
        curr->size += curr->next->size + sizeof(struct meta_data);
        
        if (curr->next->next != NULL) {
            curr->next->next->prev = curr;
        }
        curr->next = curr->next->next;
    }
}

struct meta_data * get_free_space(size_t size) {
    struct meta_data * curr = head;
    struct meta_data * last = NULL;
    while (curr != NULL) {
        if (curr->free) {
        
            if (curr->size > size + sizeof(struct meta_data)) {
                
                char * new_elem_addr = (char *)curr->chunk + size;
                struct meta_data * new_elem = (struct meta_data *)new_elem_addr;
                
                new_elem->free = true;
                new_elem->prev = curr;
                new_elem->next = curr->next;
                if (curr->next != NULL) {
                    curr->next->prev = new_elem;
                }

                new_elem->unused_space_size = 0;
                new_elem->size = curr->size - size;

                curr->free = false;
                curr->size = size;
                curr->unused_space_size = 0;
                curr->next = new_elem;

                return curr;
                
            } else if (curr->size >= size){
                curr->free = false;
                curr->unused_space_size = curr->size - size;
                curr->size = size;  
                return curr;
            }
        }

        if (curr->next == NULL) {
            last = curr;
        }
        curr = curr->next;
    }

    struct meta_data * new_elem = (struct meta_data *) sbrk(size + sizeof(struct meta_data));
    if (new_elem == -1) {
        return NULL;
    }

    new_elem->free = false;
    new_elem->size = size;
    new_elem->next = NULL;
    new_elem->unused_space_size = 0;
    new_elem->prev = last;
    
    
    if (last != NULL) {
        last->next = new_elem;
    } else {
        head = new_elem;
    }

    return new_elem;
}