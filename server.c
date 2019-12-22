#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef __APPLE__
#define QUEUE_T "kqueue"
#include <sys/event.h>
#elif __linux__
#define QUEUE_T "epoll"
#include <sys/epoll.h>
#endif

#include "client.h"
#include "server.h"
#include "vprint.h"
#include "get_ip_addr.h"
#include "http.h"
#include "util.h"


static int connect_server(struct server *server);

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

    server->qfd =
#ifdef __APPLE__
                  kqueue();
#elif __linux__
                  epoll_create(1);
#endif
    if (server->qfd < 0) {
        printf("Unable to initialize " QUEUE_T ", reason: %s\n",
                strerror(errno));
    }

    clear_mt_context(&server->mt);

    vprintf("Num cpus: %d\n", get_n_cpus());

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
    int ret = 0;

    exit_mt_routine(&server->mt);

    if (close(server->sockfd) < 0) {
        printf("Closing socket fd %d failed, reason: %s\n",
                server->sockfd, strerror(errno));
        ret = -1;
    }

    if (close(server->qfd) < 0) {
        printf("Closing " QUEUE_T " fd %d failed, reason: %s\n",
                server->qfd, strerror(errno));
        ret = -1;
    }

    return ret;
}


/*
 * binds the socket fd and listens on it, then adds the socket fd to
 * the event queue (epoll or kqueue)
 */
static int connect_server(struct server *server) {

    if (bind(server->sockfd, (struct sockaddr *) &server->in,
                sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Unable to bind socket to port %d, reason %s\n",
                ntohs(server->in.sin_port), strerror(errno));
        close_server(server);
        return -1;
    }

    if (listen(server->sockfd, server->backlog) == -1) {
        fprintf(stderr, "Unable to listen, reason: %s\n", strerror(errno));
        close_server(server);
        return -1;
    }

    struct kevent listen_ev;
    EV_SET(&listen_ev, server->sockfd, EVFILT_READ,
            EV_ADD | EV_DISPATCH, 0, 0, NULL);
    if (kevent(server->qfd, &listen_ev, 1, NULL, 0, NULL) == -1) {
        fprintf(stderr, "Unable to add server sockfd to " QUEUE_T
                ", reason: %s\n", strerror(errno));
        close_server(server);
        return -1;
    }

    return 0;
}


static int accept_connection(struct server *server) {
    struct client *client;
    struct kevent changelist[2];
    int ret;

    client = (struct client *) malloc(sizeof(struct client));
    if (client == NULL) {
        return -1;
    }

    ret = accept_client(client, server->sockfd, O_NONBLOCK);
    if (ret == -1) {
        free(client);
        return ret;
    }

    EV_SET(&changelist[0], client->connfd, EVFILT_READ,
            EV_ADD | EV_DISPATCH, 0, 0, client);
    EV_SET(&changelist[1], server->sockfd, EVFILT_READ,
            EV_ENABLE | EV_DISPATCH, 0, 0, NULL);
    if (kevent(server->qfd, changelist, 2, NULL, 0, NULL) == -1) {
        fprintf(stderr, "Unable to add client fd %d to " QUEUE_T
                ", reason: %s\n", client->connfd, strerror(errno));
        free(client);
        return -1;
    }

    return 0;
}


#define SIZE 1024

static void* _run(void *server_arg) {
    struct server *server = (struct server *) server_arg;
    struct client *client;
    struct kevent event;
    int ret;
    ssize_t nbytes;

    static int threadno = 0;

    int thread = __atomic_fetch_add(&threadno, 1, __ATOMIC_ACQ_REL);

    char msg[] = "thread 0 begin\n";
    msg[7] += thread;
    write(STDOUT_FILENO, msg, sizeof(msg));

    while (1) {
        if ((ret = kevent(server->qfd, NULL, 0, &event, 1, NULL)) == -1) {
            fprintf(stderr, QUEUE_T " call failed, reason: %s\n",
                    strerror(errno));
            return NULL;
        }
        if (event.ident == server->sockfd) {
            vprintf("Thread %d accepting...\n", thread);
            accept_connection(server);
        }
        else {
            client = (struct client *) event.udata;
            if (event.flags & EV_EOF) {
                vprintf("Thread %d disconnected %d\n", thread, client->connfd);

                //dmsg_print(&client->log, STDOUT_FILENO);
                write(STDOUT_FILENO, P_CYAN, sizeof(P_CYAN) - 1);
                dmsg_write(&client->log, STDOUT_FILENO);
                write(STDOUT_FILENO, P_RESET, sizeof(P_RESET) - 1);


                EV_SET(&event, client->connfd, EVFILT_READ,
                        EV_DELETE, 0, 0, NULL);
                if (kevent(server->qfd, &event, 1, NULL, 0, NULL) == -1) {
                    fprintf(stderr, "Unable to delete client fd %d to "
                            QUEUE_T ", reason: %s\n", client->connfd,
                            strerror(errno));
                }
                close_client(client);
                free(client);
            }
            else {
                vprintf("Thread %d reading...\n", thread);
                nbytes = receive_bytes_n(client, 128);
                EV_SET(&event, client->connfd, EVFILT_READ,
                        EV_ENABLE | EV_DISPATCH, 0, 0, client);
                if (kevent(server->qfd, &event, 1, NULL, 0, NULL) == -1) {
                    fprintf(stderr, "Unable to re-enable client fd %d to "
                            QUEUE_T ", reason: %s\n", client->connfd,
                            strerror(errno));
                }
            }
        }
    }
}

int run_server(struct server *server) {

    if (init_mt_context(&server->mt, 2, &_run, server, 0) == -1) {
        return -1;
    }

    return 0;
}
