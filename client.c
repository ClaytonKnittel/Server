#include <string.h>
#include <unistd.h>

#include "client.h"
#include "vprint.h"

int accept_client(struct client *client, int sockfd) {
    socklen_t len = sizeof(client->sa);
    memset(&client->sa, 0, len);
    client->connfd = accept(sockfd, &client->sa, &len);

    vprintf("Connected to client of type %hx, len %d\n\n",
            client->sa.sa_family, len);

    dmsg_init(&client->log);
    return 0;
}

int close_client(struct client *client) {
    close(client->connfd);
    return 0;
}

