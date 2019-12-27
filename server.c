#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
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


#ifdef __linux__

// each epoll event will have its data field point to a client struct, but
// for connections which are not clients, only the connfd field will be used
typedef struct client epoll_data_ptr_t;

#endif


static int connect_server(struct server *server);

int init_server(struct server *server, int port) {
    return init_server3(server, port, DEFAULT_BACKLOG);
}

int init_server3(struct server *server, int port, int backlog) {
    int ret = 0;

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
                  epoll_create1(0);
#endif
    if (server->qfd < 0) {
        printf("Unable to initialize " QUEUE_T ", reason: %s\n",
                strerror(errno));
        ret = -1;
    }

    server->running = 1;

    if (pipe(server->term_pipe) == -1) {
        printf("Unable to initialize pipe, reason: %s\n",
                strerror(errno));
        ret = -1;
    }

    clear_mt_context(&server->mt);

    vprintf("Num cpus: %d\n", get_n_cpus());

    if (connect_server(server) == -1) {
        ret = -1;
    }
    
    if (ret != 0) {
        close(server->sockfd);
    }
    return ret;
}

void print_server_params(struct server *server) {
    uint16_t port;

    port = ntohs(server->in.sin_port);

    vprintf("Server listening on port: %s:%d\n", get_ip_addr_str(), port);
}

void close_server(struct server *server) {
    // TODO this may be called in an interrupt context
    vprintf("Closing server on fd %d\n", server->sockfd);

    server->running = 0;

    // write to the term pipe so all threads will be notified that the server
    // is shutting down
    write(server->term_write, "x", 1);

    // join with all threads
    exit_mt_routine(&server->mt);

    if (close(server->sockfd) < 0) {
        printf("Closing socket fd %d failed, reason: %s\n",
                server->sockfd, strerror(errno));
    }

    if (close(server->qfd) < 0) {
        printf("Closing " QUEUE_T " fd %d failed, reason: %s\n",
                server->qfd, strerror(errno));
    }

    if (close(server->term_read) < 0) {
        printf("Closing term read fd %d failed, reason: %s\n",
                server->term_read, strerror(errno));
    }
    
    if (close(server->term_write) < 0) {
        printf("Closing term write fd %d failed, reason: %s\n",
                server->term_write, strerror(errno));
    }
}


/*
 * binds the socket fd and listens on it, then adds the socket fd to
 * the event queue (epoll or kqueue)
 * 
 * returns 0 on success and -1 on failure
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

#ifdef __APPLE__
    struct kevent listen_ev[2];
    EV_SET(&listen_ev[0], server->sockfd, EVFILT_READ,
            EV_ADD | EV_DISPATCH, 0, 0, NULL);
    EV_SET(&listen_ev[1], server->term_read, EVFILT_READ,
            EV_ADD, 0, 0, NULL);
    if (kevent(server->qfd, listen_ev, 2, NULL, 0, NULL) == -1) {
        fprintf(stderr, "Unable to add server sockfd and term pipe read to "
                QUEUE_T ", reason: %s\n", strerror(errno));
        close_server(server);
        return -1;
    }
#elif __linux__
    struct epoll_event listen_ev = {
#ifdef EPOLLEXCLUSIVE
        .events = EPOLLIN | EPOLLEXCLUSIVE,
#else
        .events = EPOLLIN | EPOLLONESHOT,
#endif
        // safe as long as the rest of data is never accessed, which it
        // shouldn't be for the sockfd
        .data.ptr = ((char*) &server->sockfd)
            - offsetof(epoll_data_ptr_t, connfd)
    };
    epoll_ctl(server->qfd, EPOLL_CTL_ADD, server->sockfd, &listen_ev);
    struct epoll_event term_ev = {
        .events = EPOLLIN,
        .data.ptr = ((char*) &server->term_read)
            - offsetof(epoll_data_ptr_t, connfd)
    };
    epoll_ctl(server->qfd, EPOLL_CTL_ADD, server->term_read, &term_ev);
#endif

    return 0;
}


static int accept_connection(struct server *server) {
    struct client *client;
    int ret;

    if (!server->running) {
        return -1;
    }

    client = (struct client *) malloc(sizeof(struct client));
    if (client == NULL) {
        return -1;
    }

    ret = accept_client(client, server->sockfd, O_NONBLOCK);
    if (ret == -1) {
        free(client);
        return ret;
    }

#ifdef __APPLE__
    struct kevent changelist[2];
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
#elif __linux__
    struct epoll_event event = {
#ifdef EPOLLEXCLUSIVE
        .events = EPOLLIN | EPOLLRDHUP | EPOLLEXCLUSIVE,
#else
        .events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT,
#endif
        .data.ptr = client
    };
    epoll_ctl(server->qfd, EPOLL_CTL_ADD, client->connfd, &event);
#ifndef EPOLLEXCLUSIVE
    struct epoll_event listen_ev = {
        .events = EPOLLIN | EPOLLONESHOT,
        .data.ptr = ((char*) &server->sockfd)
            - offsetof(epoll_data_ptr_t, connfd)
    };
    epoll_ctl(server->qfd, EPOLL_CTL_MOD, server->sockfd, &listen_ev);
#endif
#endif

    return 0;
}


static int read_from(struct server *server, struct client *client, int thread) {
    vprintf("Thread %d reading...\n", thread);

    receive_bytes_n(client, 4);
#ifdef __APPLE__
    struct kevent event;
    EV_SET(&event, client->connfd, EVFILT_READ,
            EV_ENABLE | EV_DISPATCH, 0, 0, client);
    if (kevent(server->qfd, &event, 1, NULL, 0, NULL) == -1) {
        fprintf(stderr, "Unable to re-enable client fd %d to "
                QUEUE_T ", reason: %s\n", client->connfd,
                strerror(errno));
    }
#elif __linux__
#ifndef EPOLLEXCLUSIVE
    struct epoll_event read_ev = {
        .events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT,
        .data.ptr = client
    };
    epoll_ctl(server->qfd, EPOLL_CTL_MOD, client->connfd, &read_ev);
#endif
#endif
    return 0;
}


static int disconnect(struct server *server, struct client *client, int thread) {
    vprintf("Thread %d disconnected %d\n", thread, client->connfd);

    //dmsg_print(&client->log, STDOUT_FILENO);
    write(STDOUT_FILENO, P_CYAN, sizeof(P_CYAN) - 1);
    dmsg_write(&client->log, STDOUT_FILENO);
    write(STDOUT_FILENO, P_RESET, sizeof(P_RESET) - 1);

#ifdef DEBUG
    char buf[4096];
    buf[4] = '\0';
    dmsg_cpy(&client->log, buf);
#endif

#ifdef __APPLE__
    struct kevent event;
    EV_SET(&event, client->connfd, EVFILT_READ,
            EV_DELETE, 0, 0, NULL);
    if (kevent(server->qfd, &event, 1, NULL, 0, NULL) == -1) {
        fprintf(stderr, "Unable to delete client fd %d to "
                QUEUE_T ", reason: %s\n", client->connfd,
                strerror(errno));
    }
#elif __linux__
    epoll_ctl(server->qfd, EPOLL_CTL_DEL, client->connfd, NULL);
#endif

    close_client(client);
    free(client);

#ifdef DEBUG
    if (strcmp(buf, "exit") == 0) {
        kill(getpid(), SIGINT);
    }
#endif
    return 0;
}


#define SIZE 1024

static void* _run(void *server_arg) {
    struct mt_args *args = (struct mt_args *) server_arg;
    struct server *server = (struct server *) args->arg;
    struct client *client;
#ifdef __APPLE__
    struct kevent event;
#elif __linux__
    struct epoll_event event;
#endif
    int ret, fd;

    int thread = args->thread_id;;

    char msg[] = "thread 0 begin\n";
    msg[7] += thread;
    write(STDOUT_FILENO, msg, sizeof(msg));

    while (1) {
        if ((ret =
#ifdef __APPLE__
                    kevent(server->qfd, NULL, 0, &event, 1, NULL)
#elif __linux__
                    epoll_wait(server->qfd, &event, 1, -1)
#endif
                    ) == -1) {
            printf(QUEUE_T " call failed, reason: %s\n", strerror(errno));
        }
#ifdef __APPLE__
        fd = event.ident;
#elif __linux__
        fd = ((epoll_data_ptr_t *) event.data.ptr)->connfd;
#endif
        if (fd == server->term_read) {
            // TODO allow remaining connections to finish ?
            return NULL;
        }
        if (fd == server->sockfd) {
            if (accept_connection(server) == 0) {
                vprintf("Thread %d accepting...\n", thread);
            }
            else {
                vprintf("Thread %d denied connection\n", thread);
            }
        }
        else {
#ifdef __APPLE__
            client = (struct client *) event.udata;
            if (event.flags & EV_EOF) {
#elif __linux__
            client = (struct client *) event.data.ptr;
            if (event.events & EPOLLRDHUP) {
#endif
                disconnect(server, client, thread);
            }
            else {
                read_from(server, client, thread);
            }
        }
    }
}

int run_server(struct server *server) {

    if (init_mt_context(&server->mt, get_n_cpus(), &_run, server, MT_PARTITION) == -1) {
    //if (init_mt_context(&server->mt, 1, &_run, server, 0) == -1) {
        return -1;
    }

    return 0;
}
