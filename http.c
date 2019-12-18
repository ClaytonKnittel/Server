#include <regex.h>
#include <stdio.h>

#include "http.h"


#define CFLAGS REG_NEWLINE

#define HEADER "^\n"
static regex_t header;

int http_init() {
    if (regcomp(&header, HEADER, CFLAGS)) {
        fprintf(stderr, "Could not compile regex \"" HEADER "\"\n");
        return 1;
    }

    return 0;
}

void http_exit() {
    regfree(&header);
}

int http_parse(struct http *p, const char* req) {

    return 0;
}

