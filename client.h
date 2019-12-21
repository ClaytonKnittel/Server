#define _GNU_SOURCE

#include <sys/socket.h>
#include "dmsg.h"

struct client {
    int connfd;

    struct sockaddr sa;

    struct dmsg_list log;
};


/*
 * attempts to connect to a client with the accept system call.
 * The flags argument passed to this function is forwarded to
 * fcntl (i.e. to allow fd to be set to nonblocking)
 *
 * upon successful connection, the client struct is fully initialized
 * and ready to receive data
 */
int accept_client(struct client *client, int sockfd, int flags);

/*
 * attempts to read data from the connection fd associated with the given
 * client into the client's dmsg_list
 *
 * returns the number of bytes read, or -1 on error
 */
ssize_t receive_data(struct client *client);

/*
 * same as receive_data, except it only attempts to read up to max
 * bytes, stopping either when that limit is reached or EOF is reached
 *
 * returns the number of bytes read, or -1 on error
 */
ssize_t receive_bytes_n(struct client *client, size_t max);

int close_client(struct client *client);

