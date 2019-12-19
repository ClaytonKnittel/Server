#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "server.h"
#include "vprint.h"
#include "get_ip_addr.h"
#include "http.h"

int init_server(struct server *server, int port) {
    return init_server3(server, port, DEFAULT_BACKLOG);
}

int init_server3(struct server *server, int port, int backlog) {
    memset(&server->in, 0, sizeof(server->in));

    server->in.sin_family = AF_INET;
    server->in.sin_addr.s_addr = INADDR_ANY;
    
    server->backlog = backlog;
    server->in.sin_port = htons(port);

    server->sockfd = socket(AF_INET, SOCK_STREAM, 0);

    return connect_server(server);
}

void print_server_params(struct server *server) {
    char ip_str[INET_ADDRSTRLEN];
    uint16_t port;

    port = ntohs(server->in.sin_port);

    inet_ntop(AF_INET, &(server->in.sin_addr), ip_str, INET_ADDRSTRLEN);

    vprintf("Server listening to %s:%d\n", ip_str, port);
    vprintf("Server listening on port: %s\n", get_ip_addr_str());
}

void close_server(struct server *server) {
    sio_print("Closing server\n");
    close(server->sockfd);

    // clean up memory used by http processor
    http_exit();
}


int connect_server(struct server *server) {

    if (bind(server->sockfd, (struct sockaddr *) &server->in,
                sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Unable to bind socket to port %d, reason %s\n",
                ntohs(server->in.sin_port), strerror(errno));
        close_server(server);
        return -1;
    }

    if (listen(server->sockfd, server->backlog) == -1) {
        fprintf(stderr, "Unable to listen, reason: %s", strerror(errno));
        close_server(server);
        return -1;
    }

    return 0;
}

void start_server(struct server *server) {

    while (1) {
        struct sockaddr client;
        socklen_t len = sizeof(client);
        memset(&client, 0, len);
        int cfd = accept(server->sockfd, &client, &len);

        vprintf("Connected to client of type %hx, len %d\n\n",
                client.sa_family, len);

        char buf[129];
        while (read(cfd, buf, 128) > 0) {
            buf[128] = '\0';
            printf("%s", buf);
        }

        vprintf("Closing connection %d\n", cfd);

        close(cfd);
        break;
    }

    close_server(server);
}
