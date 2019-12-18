#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>


int main(int argc, char *argv[]) {
    struct sockaddr_in server;

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    int port = 2345;

    if (argc != 2) {
        printf("Usage: %s <ip address>:<port>\n", argv[0]);
        return 1;
    }

    char* colon = strchr(argv[1], ':');
    if (colon != NULL) {
        *colon = '\0';
        colon++;

        char *endptr;
        port = strtol(colon, &endptr, 0);
        if (*colon == '\0' || *endptr != '\0') {
            printf("Usage: %s <ip address>:<port>\n", argv[0]);
            return 2;
        }
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    if (inet_aton(argv[1], &server.sin_addr) == 0) {
        printf("Invalid ip address: %s\n", argv[1]);
        return 3;
    }
    
    printf("Connecting to %s:%d\n", inet_ntoa(server.sin_addr), port);

    if (connect(sock, (struct sockaddr*) &server, sizeof(struct sockaddr_in)) == -1) {
        printf("Unable to connect socket to port %d, reason %s\n", port, strerror(errno));
        return -1;
    }
    close(sock);
}
