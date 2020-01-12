#include <netinet/in.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include "client.h"
#include "mt.h"

#define DEFAULT_BACKLOG 50

#define DEFAULT_PORT 80


struct server {
    struct sockaddr_in in;

    struct mt_context mt;

    // list of all connected clients
    struct {
        struct client *first, *last;
    } client_list;
    // spinlock on client_list
    int client_list_lock;

    int sockfd;

    // the size of the backlog on the port which
    // the server is listening 
    int backlog;

    // file descriptor for the asynchronous polling mechanism used,
    // which is either epolling (linux) or kqueue (macos)
    int qfd;

    // set to 1 when running, and set back to 0 when the server is shut down
    // which prevents a thread which may have returned from a blocking system
    // call (like kqueue or epoll) from continuing to operate
    volatile int running;

#ifdef __linux__
    // file descriptor for the periodic timer used for closing timed-out
    // connections (for linux only)
    int timerfd;
#elif __APPLE__
    // for mac, register a timer with identifier as given below, whose value
    // is chosen to be a file descriptor we know can not possibly be added
    // to the kqueue in any scenario (as we will never listen to stdout)
#define TIMER_IDENT STDOUT_FILENO
#endif

    // this pipe is written to when the server begins shutdown. Each thread
    // which pulls this off the queue will simply place it back in the queue
    // and terminate itself gracefully
    union {
        int term_pipe[2];
        struct {
            int term_read;
            int term_write;
        };
    };

};


/*
 * constructs the server as in init_server3, with the default backlog
 */
int init_server(struct server *server, int port);

/*
 * initialize all data structures in the server struct, and construct the
 * socket fd which is to be used for connecting to this server
 */
int init_server3(struct server *server, int port, int backlog);

void print_server_params(struct server *server);

/*
 * close the socket fd of the server which was bound to listen, and deallocate
 * memory referenced by the server struct
 *
 * this method is not safe to be called within a worker thread. If the server
 * needs to be shut down within a worker thread, call kill(getpid(), SIGINT) to
 * trigger shutdown
 */
void close_server(struct server *server);

/*
 * spawns multiple threads and begins the main loop of the server, in which
 * the threads wait until notified by the kernel that either a new connection
 * has been made and is ready to be processed, or a client socket has become
 * available for writing
 *
 * returns 0 on success or nonzero on failure
 */
int run_server(struct server *server);

int run_server2(struct server *server, int nthreads);

