#ifndef _CLIENT_H
#define _CLIENT_H

#include <sys/queue.h>
#include <sys/socket.h>

#include "dmsg.h"
#include "http.h"


#define READ_COMPLETE 1
#define READ_INCOMPLETE 2

#define WRITE_INCOMPLETE 3
#define CLIENT_CLOSE_CONNECTION 4
#define CLIENT_KEEP_ALIVE 5


struct client {
    // construct for doubly-linked list of clients
    struct {
        struct client *next, *prev;
    };

    struct http http;

    // file descriptor returned by accept syscall
    int connfd;

    // the time after which this client connection is no longer guaranteed
    // to be kept alive
    struct timespec expires;

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
 * returns one of the following status codes
 *  READ_INCOMPLETE - there is still data to be read from the socket, or
 *      we are expecting to receive more data, as the message received was
 *      not complete according to the protocol being used
 *  READ_COMPLETE - the read has complete and we can now wait for a write
 *      event on the socket
 */
int receive_bytes(struct client *client);

/*
 * same as receive_data, except it only attempts to read up to max
 * bytes, stopping either when that limit is reached or EOF is reached
 *
 * returns one of the following status codes
 *  READ_INCOMPLETE - there is still data to be read from the socket, or
 *      we are expecting to receive more data, as the message received was
 *      not complete according to the protocol being used
 *  READ_COMPLETE - the read has complete and we can now wait for a write
 *      event on the socket
 */
int receive_bytes_n(struct client *client, size_t max);

/*
 * attemts to send as much data as possible across the socket connection
 * without blocking
 *
 * returns one of the following status codes
 *  WRITE_INCOMPLETE - there is still data to be written to the socket
 *  CLOSE_CONNECTION - the write has complete and we can now close the socket
 *  KEEP_ALIVE - the write has complete and we can now wait for a read
 *      event on the socket again
 */
int send_bytes(struct client *client);

/*
 * closes connection fd associated with this client and frees all memory
 * resources associated with it
 *
 * returns 0 on success, -1 if the client has already been closed
 */
int close_client(struct client *client);

#endif /* _CLIENT_H */
