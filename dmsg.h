/* Dynamic message source
 * 
 * Dynamic messages (dmsg) are an efficient way of storing
 * dynamically generated variable-sized data streams from a
 * potentailly inconsistent source
 *
 */
#ifndef _DMSG_H
#define _DMSG_H

#include <sys/uio.h>

// default size of first node of dmsg_list
#define DEFAULT_DMSG_NODE_SIZE 64

// this is definitely overkill
#ifndef MAX_DMSG_LIST_SIZE
#define MAX_DMSG_LIST_SIZE 24
#endif


/* Errors */

#define DMSG_INIT_FAIL 1
#define DMSG_ALLOC_FAIL 2
#define DMSG_OVERFLOW 3


// just so I can use my own variable names.
// also allows for safe casting from
// dmsg_node[] to iovec[]
typedef union dmsg_node {
    struct iovec _vec;
    struct {
        // pointer to heap-allocated data buffer
        void* msg;
        // how much of this message has currently been
        // written to
        size_t size;
    };
} dmsg_node;

/**
 * structure for storage of dynamically generated
 * variable-sized data streams, in which each node
 * keeps track of a portion of the data stream, and
 * each subsequent node is double the size of the
 * previous node
 */
typedef struct dmsg_list {
    // total length of the message
    size_t len;

    // number of allocated dmsg_nodes in the list
    unsigned int list_size;

    // size of the first dmsg_node message,
    // each subsequent dmsg_node's size is
    // double its predecessor's
    unsigned int _init_node_size;
    dmsg_node list[MAX_DMSG_LIST_SIZE];
} dmsg_list;

int dmsg_init(dmsg_list*);

int dmsg_init2(dmsg_list*, unsigned int init_node_size);

void dmsg_free(dmsg_list*);

int dmsg_append(dmsg_list*, void* buf, size_t count);

int dmsg_write(dmsg_list*, int fd);

#endif /* _DMSG_H */
