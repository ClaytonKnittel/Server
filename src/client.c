#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "client.h"
#include "server.h"
#include "vprint.h"



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

    http_clear(&client->http);
    dmsg_init(&client->log);
    return 0;
}

/*
 * attempts to parse what has been read of the client's request. If the
 * request was fully read, then READ_COMPLETE is returned. Otherwise,
 * READ_INCOMPLETE or READ_ERR is returned
 */
static int parse_request(struct client *client) {
    int ret = http_parse(&client->http, &client->log);

    return (ret == HTTP_DONE || ret == HTTP_ERR) ? READ_COMPLETE :
        READ_INCOMPLETE;
}

int receive_bytes(struct client *client) {
    /*ssize_t n_read =*/ dmsg_read(&client->log, client->connfd);

    return parse_request(client);
}

int receive_bytes_n(struct client *client, size_t max) {
    /*ssize_t n_read =*/ dmsg_read_n(&client->log, client->connfd, max);

    return parse_request(client);
}

int send_bytes(struct client *client) {
    int ret = http_respond(&client->http, client->connfd);

    return (ret == HTTP_ERR || ret == HTTP_CLOSE) ? CLIENT_CLOSE_CONNECTION :
        (ret == HTTP_KEEP_ALIVE) ? CLIENT_KEEP_ALIVE : WRITE_INCOMPLETE;
}

int close_client(struct client *client) {
    dmsg_free(&client->log);
    close(client->connfd);
    return 0;
}

