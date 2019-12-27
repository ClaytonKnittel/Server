#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "client.h"
#include "vprint.h"

int accept_client(struct client *client, int sockfd, int flags) {
    socklen_t len = sizeof(client->sa);
    memset(&client->sa, 0, len);
    client->connfd = accept(sockfd, &client->sa, &len);

    if (client->connfd == -1) {
        printf("Unable to accept client, reason: %s\n", strerror(errno));
        return -1;
    }

    if (fcntl(client->connfd, F_SETFL, flags) == -1) {
        printf("Unable to set file flags, reason: %s\n", strerror(errno));
        return -1;
    }

    vprintf("Connected to client of type %hx, len %d\n",
            client->sa.sa_family, len);

    dmsg_init(&client->log);
    return 0;
}

ssize_t receive_bytes(struct client *client) {
    ssize_t n_read = dmsg_read(&client->log, client->connfd);

    return n_read;
}

ssize_t receive_bytes_n(struct client *client, size_t max) {
    ssize_t n_read = dmsg_read_n(&client->log, client->connfd, max);

    return n_read;
}

int close_client(struct client *client) {
    dmsg_free(&client->log);
    return close(client->connfd);
}

