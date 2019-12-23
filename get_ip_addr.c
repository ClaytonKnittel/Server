#define _BSD_SOURCE

#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <net/if.h>
#include <unistd.h>

#define HOST_NAME_MAX 128

static struct in_addr _get_ip_addr() {
    char hostname[HOST_NAME_MAX + 1];
    struct hostent* host_entry;

    hostname[HOST_NAME_MAX] = '\0';

    if (gethostname(hostname, sizeof(hostname) - 1) == -1) {
        printf("Failed to get host name\n");
        return (struct in_addr) { .s_addr = 0 };
    }

    host_entry = gethostbyname(hostname);
    if (host_entry == NULL) {
        printf("Failed to get host entry for name %s\n", hostname);
        return (struct in_addr) { .s_addr = 0 };
    }

    //endhostent();

    return *(struct in_addr*) host_entry->h_addr_list[0];
}

uint32_t get_ip_addr() {
    return _get_ip_addr().s_addr;
}

char* get_ip_addr_str() {
    struct in_addr addr = _get_ip_addr();
    return inet_ntoa(addr);
}


