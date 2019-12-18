#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <signal.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include "vprint.h"
#include "get_ip_addr.h"
#include "http.h"
#include "dmsg.h"

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

    struct dmsg_list msg;
};


void usage(const char* program_name) {
    printf("Usage: %s [options]\n\n"
           "\t-p port\t\tthe port the server should listen on.\n"
           "\t\t\tThe default is %d\n"
           "\t-b backlog\tnumber of connections to backlog in\n"
           "\t\t\tthe listen syscall. The default is %d\n"
           "\n"
           "\t-q\t\trun in quiet mode, which only prints errors\n"
           "\t\t\t(note: to optimize out prints, #define QUIET\n"
           "\t\t\tbefore including vprint.h)\n"
           "\t-v\t\trun with verbose level 1, which prints all\n"
           "\t\t\tv*prints, but not dbg_prints. This is the default\n"
           "\t-V\t\trun with verbose level 2, which prints everything\n"
           "\n",
           program_name, DEFAULT_PORT, DEFAULT_BACKLOG);

    exit(1);
}


void close_handler(int signum);


int init_server(struct server *server, int argc, char *argv[]) {
    int c, port;
    char* endptr;

    server->backlog = DEFAULT_BACKLOG;
    port = DEFAULT_PORT;

    memset(&server->in, 0, sizeof(server->in));

    server->in.sin_family = AF_INET;
    server->in.sin_addr.s_addr = INADDR_ANY;

#define NUM_OPT \
    strtol(optarg, &endptr, 0);                 \
    if (*optarg == '\0' || *endptr != '\0') {   \
       usage(argv[0]);                          \
    }

    while ((c = getopt(argc, argv, "b:hp:qvV")) != -1) {
        switch (c) {
        case 'b':
            server->backlog = NUM_OPT;
            break;
        case 'p':
            port = NUM_OPT;
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
        case 'h':
        case '?':
            usage(argv[0]);
        default:
            return -1;
        }
    }

#undef NUM_OPT

    server->in.sin_port = htons(port);

    server->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    signal(SIGINT, close_handler);
    
    // initialize const globals in http processor
    http_init();

    return 0;
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

    printf("Types:\nAF_INET: %x\nAF_UNIX: %x\n",
            AF_INET, AF_UNIX);

    while (1) {
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
