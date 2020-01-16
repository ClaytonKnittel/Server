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
#include <sys/timerfd.h>
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

// maximum number of bytes read from a connection in one pass of the main
// server loop
#define MAX_READ_SIZE 4096


// the system clock to use for connection expiration
#define TIMER_CLOCK CLOCK_MONOTONIC

// the default number of seconds to wait without having received any data from
// a connection before killing the connection
#define DEFAULT_CONNECTION_TIMEOUT 5

// the number of seconds to wait between successive interrupts by the timer file
// descriptor, which is responsible for closing connections that have timed out
#define TIMEOUT_CLEANUP_FREQUENCY 5


#define LOCKED 0
#define UNLOCKED 1


/*
 * locks the list lock with the passed in value, usually the thread id
 */
static __inline void acq_list_lock(struct server *server) {
    int unlocked = UNLOCKED;
    // spin until unlocked
    while (!__atomic_compare_exchange_n(&server->client_list_lock, &unlocked,
                LOCKED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        unlocked = UNLOCKED;
    }
}

static __inline void rel_list_lock(struct server *server) {
    server->client_list_lock = UNLOCKED;
}


// adds client to beginning of list
static void list_insert(struct server *server, struct client *client) {
    client->next = server->client_list.first;
    client->prev = server->client_list.first->prev;
    server->client_list.first->prev = client;
    server->client_list.first = client;
    printf("insert %d, mem %p\n", client->connfd, client);
}

static void list_remove(struct client *client) {
    client->next->prev = client->prev;
    client->prev->next = client->next;
    printf("remove %d, mem %p\n", client->connfd, client);
}

#define server_as_client_node(server_ptr) \
    ((struct client *) (((char*) (server_ptr)) \
            + offsetof(struct server, client_list) \
            - offsetof(struct client, next)))


#define list_for_each(server_ptr, client_var) \
    for ((client_var) = (server_ptr)->client_list.first; \
            (client_var) != server_as_client_node(server_ptr); \
            (client_var) = (client_var)->next)


/*
 * sets the expiration timer on this client, to be called after a complete
 * request has been parsed or on initialization of a connection
 */
static void set_expiration_timer(struct client *client) {
    clock_gettime(TIMER_CLOCK, &client->expires);
    client->expires.tv_sec += DEFAULT_CONNECTION_TIMEOUT;
}


/*
 * removes the client from the client list and reinserts them at the back, and
 * updates their expiration time
 */
static void renew_client_timeout(struct server *server, struct client *client) {
    // we either need to respond to the request or wait to receive more data
    // from it, so we update the expiration time of this connection and move it
    // to the back of the client list
    acq_list_lock(server);
    list_remove(client);
    list_insert(server, client);
    
    // renew the timeout of the connection
    set_expiration_timer(client);
    rel_list_lock(server);
}




static int connect_server(struct server *server);

int init_server(struct server *server, int port) {
    return init_server3(server, port, DEFAULT_BACKLOG);
}

int init_server3(struct server *server, int port, int backlog) {
    int ret = 0;
    sigset_t sigpipe;

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
        fprintf(stderr, "Unable to initialize " QUEUE_T ", reason: %s\n",
                strerror(errno));
        ret = -1;
    }

    server->running = 1;

#ifdef __linux__
    // create a timer file descriptor which is to be placed into the queue.
    // It will be responsible for waking up a thread to check the client list
    // and close connections that have timed out periodically
    server->timerfd = timerfd_create(TIMER_CLOCK, TFD_NONBLOCK);

    if (server->timerfd == -1) {
        fprintf(stderr, "Unable to initialize timerfd, reason: %s\n",
                strerror(errno));
        ret = -1;
    }
#endif

    if (pipe(server->term_pipe) == -1) {
        fprintf(stderr, "Unable to initialize pipe, reason: %s\n",
                strerror(errno));
        ret = -1;
    }

    clear_mt_context(&server->mt);

    // make client_list circularly linked, treat list_head just as any other
    // list node
    server->client_list.first = server->client_list.last
        = server_as_client_node(server);

    server->client_list_lock = UNLOCKED;

    if (connect_server(server) == -1) {
        ret = -1;
    }

    if (ret == 0) {
        // block SIGPIPE signals so writes to a disconnected client don't
        // terminate the program
        sigemptyset(&sigpipe);
        sigaddset(&sigpipe, SIGPIPE);
        ret = pthread_sigmask(SIG_BLOCK, &sigpipe, NULL) == 0 ? 0 : -1;
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
    struct client *client, *next;
    // TODO this may be called in an interrupt context
    vprintf("Closing server on fd %d\n", server->sockfd);

    if (server->client_list_lock == LOCKED) {
        // TODO
    }

    server->running = 0;

    // write to the term pipe so all threads will be notified that the server
    // is shutting down
    write(server->term_write, "x", 1);

    // join with all threads
    exit_mt_routine(&server->mt);

    printf("conn list: [");
    client = server->client_list.first;
    while (client != server_as_client_node(server)) {
        next = client->next;
        printf("%d, ", client->connfd);

        write(STDOUT_FILENO, P_CYAN, sizeof(P_CYAN) - 1);
        dmsg_write(&client->log, STDOUT_FILENO);
        write(STDOUT_FILENO, P_RESET, sizeof(P_RESET) - 1);

        if (close_client(client) == 0) {
            // only free client if close succeeded
            free(client);
        }
        client = next;
    }
    printf("]\n");

    CHECK(close(server->sockfd));
    CHECK(close(server->qfd));
#ifdef __linux__
    CHECK(close(server->timerfd));
#endif
    CHECK(close(server->term_read));
    CHECK(close(server->term_write));

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

#ifdef __linux__
    // start up the periodic timer
    struct itimerspec timer = {
        .it_interval = {
            .tv_sec = DEFAULT_CONNECTION_TIMEOUT,
            .tv_nsec = 0
        },
        .it_value = {
            .tv_sec = DEFAULT_CONNECTION_TIMEOUT,
            .tv_nsec = 0
        }
    };
    if (timerfd_settime(server->timerfd, 0, &timer, NULL) == -1) {
        fprintf(stderr, "Unable to initialize timer, reason: %s\n",
                strerror(errno));
        return -1;
    }
#endif

#ifdef __APPLE__
    struct kevent listen_ev[3];
    EV_SET(&listen_ev[0], server->sockfd, EVFILT_READ,
            EV_ADD | EV_DISPATCH, 0, 0, NULL);
    EV_SET(&listen_ev[1], server->term_read, EVFILT_READ,
            EV_ADD, 0, 0, NULL);
    EV_SET(&listen_ev[2], TIMER_IDENT, EVFILT_TIMER,
            EV_ADD | EV_ENABLE, NOTE_SECONDS, TIMEOUT_CLEANUP_FREQUENCY,
            NULL);
    if (kevent(server->qfd, listen_ev, 3, NULL, 0, NULL) == -1) {
        fprintf(stderr, "Unable to add server sockfd, term pipe read and "
                "timerfd to " QUEUE_T ", reason: %s\n", strerror(errno));
        close_server(server);
        return -1;
    }
#elif __linux__
    int ret;

    struct epoll_event listen_ev = {
#ifdef EPOLLEXCLUSIVE
        .events = EPOLLIN | EPOLLEXCLUSIVE,
#else
        .events = EPOLLIN | EPOLLET | EPOLLONESHOT,
#endif
        // safe as long as the rest of data is never accessed, which it
        // shouldn't be for the sockfd. Do this so each epolldata object
        // can be treated as a client object for the purposes of finding out
        // which file descriptor the event is associated with
        .data.ptr = ((char*) &server->sockfd)
            - offsetof(epoll_data_ptr_t, connfd)
    };
    ret = epoll_ctl(server->qfd, EPOLL_CTL_ADD, server->sockfd, &listen_ev);

    struct epoll_event term_ev = {
        .events = EPOLLIN,
        .data.ptr = ((char*) &server->term_read)
            - offsetof(epoll_data_ptr_t, connfd)
    };
    ret = ret == -1 ? ret :
        epoll_ctl(server->qfd, EPOLL_CTL_ADD, server->term_read, &term_ev);

    struct epoll_event timer_ev = {
        .events = EPOLLIN | EPOLLET,
        .data.ptr = ((char*) &server->timerfd)
            - offsetof(epoll_data_ptr_t, connfd)
    };
    ret = ret == -1 ? ret :
        epoll_ctl(server->qfd, EPOLL_CTL_ADD, server->timerfd, &timer_ev);

    if (ret == -1) {
        fprintf(stderr, "Unable to add server sockfd, term pipe read or "
                "timerfd to " QUEUE_T ", reason: %s\n", strerror(errno));
        return ret;
    }
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
    if (CHECK(kevent(server->qfd, changelist, 2, NULL, 0, NULL)) == -1) {
        free(client);
        return -1;
    }
#elif __linux__
#ifndef EPOLLEXCLUSIVE
    struct epoll_event listen_ev = {
        .events = EPOLLIN | EPOLLET | EPOLLONESHOT,
        .data.ptr = ((char*) &server->sockfd)
            - offsetof(epoll_data_ptr_t, connfd)
    };
    CHECK(epoll_ctl(server->qfd, EPOLL_CTL_MOD, server->sockfd, &listen_ev));
#endif
    struct epoll_event event = {
        .events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT,
        .data.ptr = client
    };
    if (CHECK(epoll_ctl(server->qfd, EPOLL_CTL_ADD, client->connfd, &event))
            == -1) {
        free(client);
        return -1;
    }
#endif

    printf("accepted on fd %d\n", client->connfd);

    acq_list_lock(server);
    // if all succeeded, then add the client to the list of all clients
    list_insert(server, client);
    // set their expiration timer while the list lock is acquired so the
    // timeout values in the client list will be nondecreasing
    set_expiration_timer(client);

    rel_list_lock(server);

    return 0;
}


static int disconnect(struct server *server, struct client *client, int thread) {
    int ret;
    vprintf("Thread %d disconnected %d\n", thread, client->connfd);

    write(STDOUT_FILENO, P_CYAN, sizeof(P_CYAN) - 1);
    dmsg_write(&client->log, STDOUT_FILENO);
    write(STDOUT_FILENO, P_RESET, sizeof(P_RESET) - 1);

#ifdef DEBUG
    char buf[5] = "\0\0\0\0";
    dmsg_cpy(&client->log, buf, sizeof(buf));
    buf[4] = '\0';
#endif

#ifdef __APPLE__
    struct kevent events[2];
    EV_SET(&events[0], client->connfd, EVFILT_READ,
            EV_DELETE, 0, 0, NULL);
    EV_SET(&events[1], client->connfd, EVFILT_WRITE,
            EV_DELETE, 0, 0, NULL);
    ret = CHECK(kevent(server->qfd, events, 2, NULL, 0, NULL) == -1);
#elif __linux__
    ret = CHECK(epoll_ctl(server->qfd, EPOLL_CTL_DEL, client->connfd, NULL));
#endif

    if (close_client(client) == 0) {
        // only free client if close succeeded
        acq_list_lock(server);
        list_remove(client);
        rel_list_lock(server);
        printf("free client %d, mem %p\n", client->connfd, client);
        free(client);
    }
    else {
        printf("Failed to cose client %d\n", client->connfd);
    }

#ifdef DEBUG
    if (strcmp(buf, "exit") == 0) {
        kill(getpid(), SIGUSR2);
    }
#endif
    return ret;
}


static int read_from(struct server *server, struct client *client, int thread) {
    int ret = receive_bytes_n(client, MAX_READ_SIZE);
    vprintf("Thread %d read from %d\n", thread, client->connfd);

    if (ret == READ_COMPLETE) {
        // need to arm the fd for writes on the connection in the queue
#ifdef __APPLE__
        struct kevent event;
        EV_SET(&event, client->connfd, EVFILT_WRITE,
               EV_ADD | EV_ENABLE | EV_DISPATCH, 0, 0, client);
        CHECK(kevent(server->qfd, &event, 1, NULL, 0, NULL) == -1);
#elif __linux__
        struct epoll_event read_ev = {
            .events = EPOLLOUT | EPOLLRDHUP | EPOLLONESHOT,
            .data.ptr = client
        };
        CHECK(epoll_ctl(server->qfd, EPOLL_CTL_MOD, client->connfd,
                    &read_ev));
#endif
    }
    else /* ret == READ_INCOMPLETE */ {
        // need to rearm the fd for reads on the connection in the queue
#ifdef __APPLE__
        struct kevent event;
        EV_SET(&event, client->connfd, EVFILT_READ,
               EV_ENABLE | EV_DISPATCH, 0, 0, client);
        CHECK(kevent(server->qfd, &event, 1, NULL, 0, NULL) == -1);
#elif __linux__
        struct epoll_event read_ev = {
            .events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT,
            .data.ptr = client
        };
        CHECK(epoll_ctl(server->qfd, EPOLL_CTL_MOD, client->connfd,
                    &read_ev));
#endif
    }

    renew_client_timeout(server, client);
    return ret;
}


static int write_to(struct server *server, struct client *client, int thread) {
    int ret = send_bytes(client);
    vprintf("Thread %d wrote to %d\n", thread, client->connfd);

    if (ret == WRITE_INCOMPLETE) {
        // need to rearm the fd for writes on the connection in the queue
#ifdef __APPLE__
        struct kevent event;
        EV_SET(&event, client->connfd, EVFILT_WRITE,
               EV_ENABLE | EV_DISPATCH, 0, 0, client);
        CHECK(kevent(server->qfd, &event, 1, NULL, 0, NULL) == -1);
#elif __linux__
        struct epoll_event read_ev = {
            .events = EPOLLOUT | EPOLLRDHUP | EPOLLONESHOT,
            .data.ptr = client
        };
        CHECK(epoll_ctl(server->qfd, EPOLL_CTL_MOD, client->connfd,
                    &read_ev));
#endif
    }
    else if (ret == CLIENT_KEEP_ALIVE) {
#ifdef __APPLE__
        struct kevent event;
        // remove write event listener
        //EV_SET(&event[0], client->connfd, EVFILT_WRITE,
        //       EV_DELETE, 0, 0, client);
        // add read event listener
        EV_SET(&event, client->connfd, EVFILT_READ,
               EV_ADD | EV_ENABLE | EV_DISPATCH, 0, 0, client);
        CHECK(kevent(server->qfd, &event, 1, NULL, 0, NULL) == -1);
#elif __linux__
        struct epoll_event read_ev = {
            .events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT,
            .data.ptr = client
        };
        CHECK(epoll_ctl(server->qfd, EPOLL_CTL_MOD, client->connfd,
                    &read_ev));
#endif
    }
    else /* ret == CLIENT_CLOSE_CONNECTION */ {
        disconnect(server, client, thread);
        return ret;
    }

    renew_client_timeout(server, client);
    return ret;
}


static void close_expired_connections(struct server *server, int thread) {
    struct client *client;
    struct timespec current_time;

    clock_gettime(TIMER_CLOCK, &current_time);

    acq_list_lock(server);
    for (client = server->client_list.last;
            client != server_as_client_node(server);
            // must keep taking from end of list because disconnect removes
            // the client from the list
            client = server->client_list.last) {
        rel_list_lock(server);

        if (timespec_after(&current_time, &client->expires)) {
            // if this client expired before the current time, we need to close
            // the connection with them
            disconnect(server, client, thread);
            acq_list_lock(server);
        }
        else {
            // because the clients are in the list in nonincreasing expiration
            // time, if one timer expires after the current time, so do all
            // others before it
            break;
        }
    }
    rel_list_lock(server);
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

    vprintf("thread %d begin\n", thread);

    while (1) {
        if ((ret =
#ifdef __APPLE__
                    kevent(server->qfd, NULL, 0, &event, 1, NULL)
#elif __linux__
                    epoll_wait(server->qfd, &event, 1, -1)
#endif
                    ) == -1) {
            fprintf(stderr, QUEUE_T " call failed on fd %d, reason: %s\n",
                    server->qfd, strerror(errno));
            continue;
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
        else if (
#ifdef __APPLE__
                 event.filter == EVFILT_TIMER
#elif __linux__
                 fd == server->timerfd
#endif
                 ) {
#ifdef __linux__
            // gotta read it so it can be rearmed
            long ntimeouts;
            read(fd, &ntimeouts, sizeof(long));
#endif
            close_expired_connections(server, thread);
        }
        else {
#ifdef __APPLE__
            client = (struct client *) event.udata;
#elif __linux__
            client = (struct client *) event.data.ptr;
#endif

            if (
#ifdef __APPLE__
                    event.filter == EVFILT_READ
#elif __linux__
                    event.events & EPOLLIN
#endif
                    ) {
                ret = read_from(server, client, thread);
            }
            else if (
#ifdef __APPLE__
                    event.filter == EVFILT_WRITE
#elif __linux__
                    event.events & EPOLLOUT
#endif
                    ) {
                ret = write_to(server, client, thread);
            }
            // after completing the read/write, check if the read-end of the
            // socket has been closed
            if (
                    (ret == READ_COMPLETE || ret == CLIENT_KEEP_ALIVE) &&
#ifdef __APPLE__
                    event.flags & EV_EOF
#elif __linux__
                    event.events & EPOLLRDHUP
#endif
                    ) {
                disconnect(server, client, thread);
            }
        }
    }
}

int run_server(struct server *server) {

    if (init_mt_context(&server->mt, get_n_cpus(), &_run, server, MT_PARTITION) == -1) {
        return -1;
    }

    return 0;
}

int run_server2(struct server *server, int nthreads) {
    if (init_mt_context(&server->mt, nthreads, &_run, server, 0) == -1) {
        return -1;
    }

    return 0;
}
