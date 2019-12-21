
#define DEFAULT_BACKLOG 50

#define DEFAULT_PORT 80


static struct server {
    struct sockaddr_in in;

    int sockfd;

    // the size of the backlog on the port which
    // the server is listening 
    int backlog;

    // file descriptor for the asynchronous polling mechanism used,
    // which is either epolling (linux) or kqueue (macos)
    int qfd;

} server;


/*
 * constructs the server as in init_server3, with the default backlog
 */
int init_server(struct server *server, int port);

/*
 * initialize all data structures in the server struct, and construct
 * the socket fd which is to be used for connecting to this server
 */
int init_server3(struct server *server, int port, int backlog);

void print_server_params(struct server *server);

/*
 * close the socket fd of the server which was bound to listen,
 * and deallocate memory referenced by the server struct
 */
int close_server(struct server *server);

/*
 * begins the main loop of the server, in which the thread waits until
 * notified by the kernel that either a new connection has been made
 * and is ready to be processed, or a client socket has become available
 * for writing
 *
 * this method is designed to be thread-safe
 */
void run_server(struct server *server);

