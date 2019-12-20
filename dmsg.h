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

/* Stream Errors */
// operation would make the seek pointer negative
#define DMSG_SEEK_NEG 4
// operation would make the seek pointer greater than the size of the list
#define DMSG_SEEK_OVERFLOW 5


/*
 * just so I can use my own variable names.
 * also allows for safe casting from
 * dmsg_node[] to iovec[]
 */
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


typedef size_t dmsg_off_t;

/*
 * structure for storage of dynamically generated
 * variable-sized data streams, in which each node
 * keeps track of a portion of the data stream, and
 * each subsequent node is double the size of the
 * previous node
 */
typedef struct dmsg_list {
    // total length of the message
    size_t len;

    /* 
     * used to track how much of the dmsg_list has been
     * read, used in the stream-like operations
     *
     * initialized to 0 and incremented for each byte
     * read by dmsg_getline, etc.
     */
    dmsg_off_t _offset;

    // number of allocated dmsg_nodes in the list
    unsigned int list_size;

    /* 
     * size of the first dmsg_node message,
     * each subsequent dmsg_node's size is
     * double its predecessor's
     *
     * TODO save as order? then size is 1 << order
     */
    unsigned int _init_node_size;
    dmsg_node list[MAX_DMSG_LIST_SIZE];
} dmsg_list;



// -------------------- Initialization --------------------

/*
 * initializes a dmsg_list with the default initial node
 * size
 *
 * returns 0 on success, nonzero on failure
 */
int dmsg_init(dmsg_list*);

/*
 * initializes a dmsg_list with the given initial node
 * size
 *
 * returns 0 on success, nonzero on failure
 */
int dmsg_init2(dmsg_list*, unsigned int init_node_size);

/*
 * cleans up the memory occupied by the dmsg_list
 */
void dmsg_free(dmsg_list*);



// -------------------- Printing --------------------

/*
 * displays the list formatted nicely and writes to
 * the supplied fd
 */
void dmsg_print(const dmsg_list*, int fd);



// -------------------- State operations --------------------

/*
 * appends data to the dmsg_list
 *
 * returns 0 on success, nonzero on failure
 */
int dmsg_append(dmsg_list*, void* buf, size_t count);

/*
 * reads from fd into the list until EOF is reached
 *
 * returns the number of bytes successfully read
 */
size_t dmsg_read(dmsg_list*, int fd);

/*
 * attempts to read up to count bytes from fd into the list, but will stop
 * if EOF is reached before then
 * 
 * returns the number of bytes successfully read
 */
size_t dmsg_read_n(dmsg_list*, int fd, int count);

/*
 * copies the dmsg_list into the supplied buffer, assuming the buffer is
 * large enough to fit all of the data from the list
 *
 * returns 0 on success, nonzero on failure
 */
int dmsg_cpy(dmsg_list*, char *buf);

/*
 * attempts to write all data from the dmsg_list object to the given file
 * descriptor
 *
 * returns 0 on success, nonzero on failure
 */
int dmsg_write(dmsg_list*, int fd);



// -------------------- Stream-like operations --------------------

/*
 * sets the offset pointer of the supplied dmsg_list according to the
 * whence option, which specifies how it is being modified
 *
 * options for whence:
 *  SEEK_SET: directly sets the offset to the supplied offset
 *  SEEK_CUR: adds the supplied offset to the current offset
 *  SEEK_END: sets the offset to the end of the dmsg_list plus
 *      the supplied offset
 *
 * returns 0 on success and nonzero on failure
 *
 * Possible errors:
 *  DMSG_SEEK_NEG: operation would cause the offset pointer to go negative
 *  DMSG_SEEK_OVERFLOW: operation would cause the offset pointer to exceed
 *      the size of the dmsg_list
 */
int dmsg_seek(dmsg_list*, ssize_t offset, int whence);

/*
 * attempts to read a line from the dmsg_list, either stopping at a newline
 * character, EOF, or after max number of characters have been read into
 * the buffer. This method terminates the line read with a null terminator,
 * but without the newline character at the end
 *
 * returns the number of bytes successfully read
 */
size_t dmsg_getline(dmsg_list*, char *buf, size_t bufsize);


#endif /* _DMSG_H */
