#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "client.h"
#include "server.h"
#include "vprint.h"


// returned by a method to indicate that the client should be disconnected
#define CLIENT_CLOSE 1


int accept_client(struct client *client, int sockfd, int flags) {
    socklen_t len = sizeof(struct sockaddr);
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

    client->keep_alive = 1;

    http_clear(&client->http);
    dmsg_init(&client->log);
    return 0;
}

/*
 * attempts to parse what has been read of the client's request. If the
 * request was fully read and responded to appropriately, then CLIENT_CLOSE
 * is returned, otherwise 0 is returned
 */
static int parse_request(struct client *client) {
    int ret = http_parse(&client->http, &client->log);
    if (ret != HTTP_NOT_DONE) {
        // either an error occured or we finished parsing the request,
        // either way we need to respond now
        http_respond(&client->http, client->connfd);
        return CLIENT_CLOSE;
    }
    return 0;
}

ssize_t receive_bytes(struct client *client) {
    ssize_t n_read = dmsg_read(&client->log, client->connfd);

    if (parse_request(client) == CLIENT_CLOSE) {
        client->keep_alive = 0;
    }

    return n_read;
}

ssize_t receive_bytes_n(struct client *client, size_t max) {
    ssize_t n_read = dmsg_read_n(&client->log, client->connfd, max);

    if (parse_request(client) == CLIENT_CLOSE) {
        client->keep_alive = 0;
    }

    return n_read;
}

int close_client(struct client *client) {
    dmsg_free(&client->log);
    close(client->connfd);
    return 0;
}

