#ifndef _CLIENT_H
#define _CLIENT_H

#include <sys/queue.h>
#include <sys/socket.h>

#include "dmsg.h"
#include "http.h"

struct client {
    LIST_ENTRY(client) list_entry;

    struct http http;

    // file descriptor returned by accept syscall
    int connfd;

    // set to 1 while the client is alive, can be set to 0 to indicate that
    // the client should be disconnected
    int keep_alive;

    // sockaddr struct associated with server
    struct sockaddr sa;

    // log of all data received from this client
    struct dmsg_list log;
};


/*
 * attempts to connect to a client with the accept system call.
 * The flags argument passed to this function is forwarded to
 * fcntl (i.e. to allow fd to be set to nonblocking)
 *
 * upon successful connection, the client struct is fully initialized
 * and ready to receive data, and 0 is returned. On error, -1 is
 * returned
 */
int accept_client(struct client *client, int sockfd, int flags);

/*
 * attempts to read data from the connection fd associated with the given
 * client into the client's dmsg_list
 *
 * returns the number of bytes read, or -1 on error
 */
ssize_t receive_bytes(struct client *client);

/*
 * same as receive_data, except it only attempts to read up to max
 * bytes, stopping either when that limit is reached or EOF is reached
 *
 * returns the number of bytes read, or -1 on error
 */
ssize_t receive_bytes_n(struct client *client, size_t max);

/*
 * closes connection fd associated with this client and frees all memory
 * resources associated with it
 *
 * returns 0 on success, -1 if the client has already been closed
 */
int close_client(struct client *client);

#endif /* _CLIENT_H */
