#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "vprint.h"
#include "get_ip_addr.h"
#include "http.h"

#define DEFAULT_BACKLOG 50

#define DEFAULT_PORT 2345


static struct server {
    struct sockaddr_in in;

    int sockfd;

    // the size of the backlog on the port which
    // the server is listening 
    int backlog;
} server;


struct client {
    
    int connfd;
};


void close_handler(int signum);


int init_server(struct server *server, int argc, char *argv[]) {
    int c, port;

    // initialize const globals in http processor
    http_init();

    signal(SIGINT, close_handler);

    server->backlog = DEFAULT_BACKLOG;
    port = DEFAULT_PORT;

    memset(&server->in, 0, sizeof(server->in));

    server->in.sin_family = AF_INET;
    server->in.sin_addr.s_addr = INADDR_ANY;

    while ((c = getopt(argc, argv, "b:p:qvV")) != -1) {
        switch (c) {
        case 'b':
            server->backlog = atoi(optarg);
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 'q':
            vlevel = V0;
            break;
        case 'v':
            vlevel = V1;
            break;
        case 'V':
            vlevel = V2;
            break;
        case '?':
            if (optopt == 'p' || optopt == 'b') {
                fprintf(stderr, "Option -%c requires an argument\n", optopt);
            }
            else if (isprint(optopt)) {
                fprintf(stderr, "Unknown option %c\n", optopt);
            }
            else {
                fprintf(stderr, "Unknown option character 0x%x\n", c);
            }
            return 1;
        default:
            return -1;
        }
    }

    server->in.sin_port = htons(port);

    server->sockfd = socket(AF_INET, SOCK_STREAM, 0);

    return 0;
}

void print_server_params(struct server *server) {
    char ip_str[INET_ADDRSTRLEN];
    uint16_t port;

    port = ntohs(server->in.sin_port);

    inet_ntop(AF_INET, &(server->in.sin_addr), ip_str, INET_ADDRSTRLEN);

    vprintf("Server created on port %s:%d\n", ip_str, port);
    vprintf("Server: %s\n", get_ip_addr_str());
}

void close_server(struct server *server) {
    sio_print("Closing server\n");
    close(server->sockfd);

    // clean up memory used by http processor
    http_exit();
}

void close_handler(int signum) {
    close_server(&server);
    exit(0);
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

int main(int argc, char *argv[]) {
    int ret;

    if ((ret = init_server(&server, argc, argv)) != 0) {
        return ret;
    }

    if ((ret = connect_server(&server)) != 0) {
        return ret;
    }
    
    print_server_params(&server);

    printf("Types:\nAF_INET: %x\nAF_UNIX: %x\nAF_NS: %x\nAF_IMPLINK: %x\n",
            AF_INET, AF_UNIX, AF_NS, AF_IMPLINK);

    while (0) {
        struct sockaddr client;
        socklen_t len = sizeof(client);
        memset(&client, 0, len);
        int cfd = accept(server.sockfd, &client, &len);

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

    close_server(&server);

    return 0;
}
