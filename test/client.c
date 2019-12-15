#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>

int main() {
    struct sockaddr_in server;

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    int port = 2345;

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_aton("127.0.0.1", &server.sin_addr);

    if (connect(sock, (struct sockaddr*) &server, sizeof(struct sockaddr_in)) == -1) {
        printf("Unable to connect socket to port %d, reason %s\n", port, strerror(errno));
        return -1;
    }
    close(sock);
}
