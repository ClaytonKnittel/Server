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
 * returns zero on success, nonzero on error
 */
int receive_data(struct client *client);

int close_client(struct client *client);

