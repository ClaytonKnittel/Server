#include <sys/socket.h>
#include "dmsg.h"

struct client {
    int connfd;

    struct sockaddr sa;

    struct dmsg_list log;
};


int accept_client(struct client *client, int sockfd);

int close_client(struct client *client);

