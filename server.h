
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


int init_server(struct server *server, int port);

int init_server3(struct server *server, int port, int backlog);

void print_server_params(struct server *server);

int close_server(struct server *server);

int connect_server(struct server *server);

void start_server(struct server *server);

