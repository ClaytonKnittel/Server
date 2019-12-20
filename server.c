#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "client.h"
#include "server.h"
#include "vprint.h"
#include "get_ip_addr.h"
#include "http.h"
#include "util.h"

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

    printf("Num cpus: %d\n", get_n_cpus());

    return connect_server(server);
}

void print_server_params(struct server *server) {
    char ip_str[INET_ADDRSTRLEN];
    uint16_t port;

    port = ntohs(server->in.sin_port);

    vprintf("Server listening on port: %s:%d\n", get_ip_addr_str(), port);
}

int close_server(struct server *server) {
    // TODO this may be called in an interrupt context
    vprintf("Closing server on fd %d\n", server->sockfd);

    if (close(server->sockfd) < 0) {
        printf("Closing socket fd %d failed, reason: %s\n",
                server->sockfd, strerror(errno));
        return -1;
    }
    return 0;
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

#define SIZE 1024
void start_server(struct server *server) {
    struct client c;
    while (1) {
        accept_client(&c, server->sockfd, O_NONBLOCK);
        printf("Open client\n");

        receive_data(&c);

        close_client(&c);
        printf("Close client\n");

        /*char buf[SIZE + 1];
        if (read(cfd, buf, SIZE) > 0) {
            buf[SIZE] = '\0';
            printf(P_CYAN "%s" P_RESET, buf);
            char msg[] =    "HTTP/1.1 200 OK\n"
                            "Date: Thu, 19 Dec 2019 10:46:30 GMT\n"
                            "Server: Clayton/0.1\n"
                            "Last-Modified: Mon, 02 Sep 2019 02:29:25 GMT\n"
                            "Content-Length: 24\n"
                            "Content-Type: text/plain; charset=UTF-8n\n\n"
                            "Hey this is a response!\n";
            write(cfd, msg, sizeof(msg) - 1);
            //break;
        }

        vprintf("Closing connection %d\n", cfd);*/

        /*if (close(cfd) < 0) {
            printf("Closing socket fd %d failed, reason: %s\n",
                    cfd, strerror(errno));
        }*/
    }

    close_server(server);
}
