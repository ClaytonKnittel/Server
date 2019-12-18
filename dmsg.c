#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dmsg.h"


static size_t min(size_t a, size_t b) {
    return (a < b) ? a : b;
}


static int dmsg_grow(dmsg_list *list);


int dmsg_init(dmsg_list *list) {
    return dmsg_init2(list, DEFAULT_DMSG_NODE_SIZE);
}

int dmsg_init2(dmsg_list *list, unsigned int init_node_size) {
    int ret;

    list->len = 0;
    list->list_size = 1;
    list->_init_node_size = init_node_size;

    if (init_node_size & (init_node_size - 1)) {
        printf("Initial node size of dmsg list must be a power of 2, but"
                "got %u\n", init_node_size);
        return DMSG_INIT_FAIL;
    }

    if ((ret = dmsg_grow(list)) != 0) {
        return ret;
    }
    list->list[0].size = 0;

    return 0;
}

void dmsg_free(dmsg_list *list) {
    for (unsigned int i = 0; i < list->list_size; i++) {
        free(list->list[i].msg);
    }
}



// Gives the size of the dmsg_node at the given index,
// given the size of the first node
static size_t dmsg_node_size(unsigned int init_node_size, unsigned int idx) {
    return ((size_t) init_node_size) << idx;
}

// Given the initial node size and number of nodes allocated,
// gives the total number of bytes of data that can be held in
// those nodes.
//
// This is reliant on init_node_size being a power of 2
static size_t dmsg_size(unsigned int init_node_size, unsigned int num_nodes) {
    return (dmsg_node_size(init_node_size, num_nodes) - 1) &
        ~(init_node_size - 1);
}

// Calculates how many bytes of data can be written to the last
// node before needing to allocate another dmsg_node
static size_t dmsg_remainder(dmsg_list *list) {
    return dmsg_size(list->_init_node_size, list->list_size) - list->len;
}

static dmsg_node* dmsg_last(dmsg_list *list) {
    return &list->list[list->list_size - 1];
}

// Returns a pointer to the memory address immediately after the
// end of written data 
static void* dmsg_end(dmsg_list *list) {
    return ((char*) dmsg_last(list)->msg) +
        (list->len - dmsg_size(list->_init_node_size, list->list_size - 1));
}

static int dmsg_grow(dmsg_list *list) {
    int idx = list->list_size;
    size_t size;
    
    if (idx == MAX_DMSG_LIST_SIZE) {
        return DMSG_OVERFLOW;
    }

    size = dmsg_node_size(list->_init_node_size, idx);

    list->list[idx].msg = malloc(size);
    if (list->list[idx].msg == NULL) {
        return DMSG_ALLOC_FAIL;
    }
    list->list[idx].size = 0;

    list->list_size++;
    return 0;
}


// appends data to the dmsg_list
int dmsg_append(dmsg_list *list, void* buf, size_t count) {
    size_t remainder, write_size;

    remainder = dmsg_remainder(list);
    
    write_size = min(remainder, count);
    count -= write_size;

    memcpy(dmsg_end(list), buf, write_size);
    dmsg_last(list)->size += write_size;

    if (count > 0) {
        dmsg_grow(list);
        if (remainder != count) {
            return dmsg_append(list, ((char*) buf) + write_size, count);
        }
    }
    return 0;
}


// writes all data in the dmsg to the given file descriptor
int dmsg_write(dmsg_list *list, int fd) {
    ssize_t written;
    if ((written = writev(fd, (struct iovec *) list->list, list->list_size))
            < list->len) {
        printf("Unable to write all %lu bytes of dmsg_list to fd %d,\n"
               "writev returned %ld\n", list->len, fd, written);
        return 1;
    }
    return 0;
}

