#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "client.h"
#include "vprint.h"

int accept_client(struct client *client, int sockfd, int flags) {
    socklen_t len = sizeof(client->sa);
    memset(&client->sa, 0, len);
    client->connfd = accept(sockfd, &client->sa, &len);

    fcntl(client->connfd, F_SETFD, flags);

    vprintf("Connected to client of type %hx, len %d\n\n",
            client->sa.sa_family, len);

    dmsg_init(&client->log);
    return 0;
}

int receive_data(struct client *client) {
    int ret = (dmsg_read(&client->log, client->connfd) == 0);

    dmsg_write(&client->log, STDOUT_FILENO);

    return ret;
}

int close_client(struct client *client) {
    close(client->connfd);
    return 0;
}

