// for dprintf on linux
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dmsg.h"
#include "util.h"


static size_t min(size_t a, size_t b) {
    return (a < b) ? a : b;
}


static int dmsg_grow(dmsg_list *list);




// -------------------- Initialization --------------------


int dmsg_init(dmsg_list *list) {
    return dmsg_init2(list, DEFAULT_DMSG_NODE_SIZE);
}

int dmsg_init2(dmsg_list *list, unsigned int init_node_size) {
    int ret;

    if (init_node_size == 0 || init_node_size & (init_node_size - 1)) {
        printf("Initial node size of dmsg list must be a power of 2, but "
                "got %u\n", init_node_size);
        return DMSG_INIT_FAIL;
    }

    __builtin_memset(list, 0, offsetof(dmsg_list, _init_node_size));
    list->_init_node_size = init_node_size;

    if ((ret = dmsg_grow(list)) != 0) {
        return ret;
    }

    return 0;
}

void dmsg_free(dmsg_list *list) {
    for (unsigned int i = 0; i < list->alloc_size; i++) {
        free(list->list[i].msg);
    }
}



// -------------------- Node size/ops helper methods --------------------


/*
 * gives the size of the dmsg_node at the given index,
 * given the size of the first node
 */
static size_t dmsg_node_size(unsigned int init_node_size, unsigned short idx) {
    return ((size_t) init_node_size) << idx;
}

/*
 * given the initial node size and number of nodes allocated,
 * gives the total number of bytes of data that can be held in
 * those nodes.
 */
static size_t dmsg_size(unsigned int init_node_size, unsigned short num_nodes) {
    return (dmsg_node_size(init_node_size, num_nodes) - 1) &
        ~(init_node_size - 1);
}


/*
 * calculates how many bytes of data can be written to the current
 * node before needing to write to the next dmsg_node
 */
static size_t dmsg_remainder(dmsg_list *list) {
    return dmsg_size(list->_init_node_size, list->list_size) - list->len;
}


/*
 * returns a pointer to the last allocated node of the list
 */
static dmsg_node* dmsg_last(dmsg_list *list) {
    return &list->list[list->list_size - 1];
}

/* 
 * returns a pointer to the memory address immediately after the
 * end of written data 
 */
static void* dmsg_end(dmsg_list *list) {
    return ((char*) dmsg_last(list)->msg) +
        (list->len - dmsg_size(list->_init_node_size, list->list_size - 1));
}

static int dmsg_grow(dmsg_list *list) {
    unsigned short idx = list->list_size;
    size_t size;

    if (idx < list->alloc_size) {
        list->list[idx].size = 0;
        list->list_size++;
        return 0;
    }
    if (idx == MAX_DMSG_LIST_SIZE) {
        return DMSG_OVERFLOW;
    }

    size = dmsg_node_size(list->_init_node_size, idx);

    list->list[idx].msg = malloc(size);
    if (list->list[idx].msg == NULL) {
        return DMSG_ALLOC_FAIL;
    }
    list->list[idx].size = 0;

    // TODO make one op
    list->alloc_size++;
    list->list_size++;
    return 0;
}



// -------------------- Printing --------------------

void dmsg_print(const dmsg_list *list, int fd) {
    unsigned int i;
    int wid, base;

    dprintf(fd, "dmsg_list:\n\tmsg len: %lu\n\tnum alloced list nodes: %u\n"
            "\tfirst node size: %u\n",
            list->len, list->alloc_size, list->_init_node_size);

    base = first_set_bit(list->_init_node_size);
    wid = dec_width((list->list_size - 1) + base);
    for (i = 0; i < list->list_size; i++) {
        dprintf(fd, " node %2u [ %*lu / %-*lu ]:\t%.*s...\n",
                i, wid, list->list[i].size, wid,
                dmsg_node_size(list->_init_node_size, i),
                (int) min(list->list[i].size, 32), (char*) list->list[i].msg);
    }
}


// -------------------- State operations --------------------

int dmsg_append(dmsg_list *list, void* buf, size_t count) {
    size_t remainder, write_size;

    while (count > 0) {
        remainder = dmsg_remainder(list);
        
        write_size = min(remainder, count);
        count -= write_size;

        memcpy(dmsg_end(list), buf, write_size);
        dmsg_last(list)->size += write_size;
        list->len += write_size;

        if (remainder == write_size && write_size > 0) {
            // we filled up this buffer
            dmsg_grow(list);
            buf += write_size;
        }
    }
    return 0;
}

size_t dmsg_read(dmsg_list *list, int fd) {
    ssize_t remainder, read_size, total_read = 0;

    while (1) {
        remainder = dmsg_remainder(list);

        read_size = read(fd, dmsg_end(list), remainder);
        if (read_size == -1) {
            total_read = (total_read == 0) ? read_size : total_read;
            break;
        }
        dmsg_last(list)->size += read_size;
        list->len += read_size;

        total_read += read_size;

        if (read_size == remainder) {
            // we filled up this buffer
            dmsg_grow(list);
        }
        else {
            break;
        }
    }
    return total_read;
}

size_t dmsg_read_n(dmsg_list *list, int fd, size_t count) {
    ssize_t remainder, req_size, read_size = 0, total_read = 0;

    while (count > 0) {
        remainder = dmsg_remainder(list);

        req_size = min(remainder, count);

        read_size = read(fd, dmsg_end(list), req_size);
        if (read_size == -1) {
            total_read = (total_read == 0) ? read_size : total_read;
            break;
        }
        dmsg_last(list)->size += read_size;
        list->len += read_size;
        count -= read_size;

        total_read += read_size;

        if (read_size == remainder) {
            // we filled up this buffer
            dmsg_grow(list);
        }
        if (read_size != req_size) {
            // we have read everything from the stream
            break;
        }
    }
    return total_read;
}

int dmsg_cpy(dmsg_list *list, char *buf) {
    size_t size;
    for (int i = 0; i < list->list_size; i++) {
        size = list->list[i].size;
        memcpy(buf, list->list[i].msg, size);
        buf += size;
    }
    return 0;
}

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



// -------------------- dmsg_offset_t operations --------------------

/*
 * returns the index of the node in which the character at the given
 * offset would appear in in a dmsg_list with given initial node size
 */
static int dmsg_offset_idx(
        unsigned int init_node_size, dmsg_off_t offset) {

    int order = first_set_bit(init_node_size);

    offset |= (1LU << order) - 1;
    offset++;

    int index = last_set_bit(offset) - order;
    return index;
}

/*
 * returns a pointer to the memory address of the char at location idx
 * in the dmsg_list
 */
/*static void* dmsg_at(dmsg_list *list, dmsg_off_t idx) {
    int list_idx = dmsg_offset_idx(list->_init_node_size, idx);
    size_t len = dmsg_size(list->_init_node_size, idx);

    return ((char*) (list->list[list_idx].msg)) + (idx - len);
}*/



// -------------------- Stream-like operations --------------------


int dmsg_seek(dmsg_list *list, ssize_t offset, int whence) {
    dmsg_off_t new_off;
    switch (whence) {
    case SEEK_SET:
        if (offset < 0) {
            return DMSG_SEEK_NEG;
        }
        if (offset > list->len) {
            return DMSG_SEEK_OVERFLOW;
        }
        list->_offset = offset;
        break;
    case SEEK_CUR:
        new_off = list->_offset + offset;
        if (new_off < 0) {
            return DMSG_SEEK_NEG;
        }
        if (new_off > list->len) {
            return DMSG_SEEK_OVERFLOW;
        }
        list->_offset = new_off;
        break;
    case SEEK_END:
        new_off = list->len + offset;
        if (new_off < 0) {
            return DMSG_SEEK_NEG;
        }
        if (new_off > list->len) {
            return DMSG_SEEK_OVERFLOW;
        }
        list->_offset = new_off;
        break;
    default:
        return EINVAL;
    }
    return 0;
}

/*
 * searches from the given offset in the list for the given delimiter
 */
static int dmsg_search(dmsg_list *list, size_t offset, char del) {
    return 1;
}

size_t dmsg_getline(dmsg_list *list, char *buf, size_t bufsize) {
    size_t remainder, msg_len = 0;
    const size_t ibufsize = bufsize;
    char *newline, *msg_loc;
    unsigned int idx, init_node_size;

    init_node_size = list->_init_node_size;

    // we cannot read more characters than there are in the dmsg_list
    bufsize = min(bufsize, list->len - list->_offset);

    idx = dmsg_offset_idx(init_node_size, list->_offset);
    remainder = dmsg_size(init_node_size, idx + 1) - list->_offset;
    msg_loc = list->list[idx].msg +
        (list->_offset - dmsg_size(init_node_size, idx));

    while (1) {
        remainder = min(remainder, bufsize);
        newline = (char*) memchr(msg_loc, '\n', remainder);

        // length of the message segment to be read
        size_t len = newline == NULL ? remainder :
            (((size_t) newline) + 1 - ((size_t) msg_loc));

        // copy the contents into the buffer
        memcpy(buf + msg_len, msg_loc, len);

        msg_len += len;
        bufsize -= len;

        // either we found a newline character or exhausted the buffer
        if (newline != NULL || bufsize == 0) {
            break;
        }

        idx++;
        remainder = dmsg_node_size(init_node_size, idx);
        msg_loc = list->list[idx].msg;
    }

    if (buf[msg_len - 1] != '\n') {
        if (msg_len == ibufsize) {
            // TODO keep track of last location searched to if no newline is
            // found. That way don't keep searching when only receiving 1 byte
            // at a time
            // TODO keep track of last location of newline if one was found,
            // so very large messages aren't traversed multiple times
            if (dmsg_search(list, list->_offset + msg_len, '\n')) {
                // if there is a newline somewhere, then this was a partial
                // read
                errno = DMSG_PARTIAL_READ;
                // if we filled the buffer and the last character read in
                // was not a newline, then we need to decrement the offset by
                // one, since we'll be overwriting that last character
                list->_offset--;
            }
            else {
                // otherwise we tried reading in a line that hasn't been
                // completely written yet
                msg_len = 0;
                errno = DMSG_NO_NEWLINE;
                return msg_len;
            }
        }
        else {
            // if we did not fill the buffer, then we return nothing and set
            // errno
            msg_len = 0;
            errno = DMSG_NO_NEWLINE;
            return msg_len;
        }
    }
    else {
        errno = 0;
    }

    list->_offset += msg_len;

    buf[msg_len - 1] = '\0';

    return msg_len;
}


void dmsg_consolidate(dmsg_list *list) {
    list->_cutoff_offset = list->_offset;
}

