#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "http.h"
#include "util.h"

// terminology from:
// https://www.w3.org/Protocols/HTTP/1.1/rfc2616bis/draft-lafon-rfc2616bis-03.html


#define CFLAGS 0

// size of buffer to hold each line from request
#define MAX_LINE 256



// ^(OPTIONS|GET|POST) (?:([^\s\?]+)(?:\?([^\s\?]*))?) (HTTP\/1.0|HTTP\/1.1)$


#define HEADER "^(" METHOD_OPTS ") " URI " ()"


// relative group offsets in URI:
//  group 1: absolute URI heir part net path
//  group 2: absolute URI heir part abs path
//  group 3: absolute URI opaque part
//  group 4: relative URI net path
//  group 5: relative URI abs path
//  group 6: relative URI rel path

#define URI "(?:(?:" ABSOLUTE_URI "|" RELATIVE_URI ")(?:#" FRAGMENT ")?)"
#define ABSOLUTE_URI "(?:" SCHEME ":(?:" HEIR_PART "|" OPAQUE_PART "))"
#define RELATIVE_URI "(?:" NET_PATH "|" ABS_PATH "|" REL_PATH ")" \
    "(?:\\?" QUERY ")?"

#define HEIR_PART "(?:" NET_PATH "|" ABS_PATH")"
// captures
#define OPAQUE_PART "(" URIC_NO_SLASH URIC "*)"

#define URIC_NO_SLASH "(?:[" UNRESERVED ";\\?:@&=\\+\\$,]|" ESCAPED ")"

#define NET_PATH "(?:\\/\\/" AUTHORITY ABS_PATH "?)"
// captures
#define ABS_PATH "(\\/" PATH_SEGMENTS ")"
#define ABS_PATH_NOCAPTURE "(?:\\/" PATH_SEGMENTS ")"
#define REL_PATH "(" REL_SEGMENT ABS_PATH_NOCAPTURE "?)"

#define REL_SEGMENT "(?:[" UNRESERVED ";@&=\\+\\$,]|" ESCAPED ")+"

#define SCHEME "(?:[" ALPHA "][" ALPHA DIGIT "\\+-\\.]*)"

#define AUTHORITY "(?:" SERVER "|" REG_NAME ")"

#define REG_NAME "(?:[" UNRESERVED "\\$,;:@&=\\+]|" ESCAPED ")+"

#define SERVER "(?:(?:" USERINFO "@)?" HOSTPORT ")?"
#define USERINFO "(?:[" UNRESERVED ";:&=\\+\\$,]|" ESCAPED ")*"

#define HOSTPORT "(?:" HOST "(?::" PORT ")?)"
#define HOST "(?:" HOSTNAME "|" IPV4ADDRESS ")"
#define HOSTNAME "(?:(?:" DOMAINLABEL "\\.)*" TOPLABEL "\\.?)"
#define DOMAINLABEL "(?:[" ALPHANUM "](?:[" ALPHANUM "-]+[" ALPHANUM "])?)"
#define TOPLABEL "(?:[" ALPHA "](?:[" ALPHANUM "-]+[" ALPHANUM "])?)"
#define IPV4ADDRESS "(?:[" DIGIT "]+\\.[" DIGIT "]+\\.[" DIGIT "]+" \
    "\\.[" DIGIT "]+)"
#define PORT "[" DIGIT "]*"

#define PATH "(?:" ABS_PATH "|" OPAQUE_PART ")?"
#define PATH_SEGMENTS "(?:" SEGMENT "(?:\\/" SEGMENT ")*)"
#define SEGMENT "(?:" PCHAR "*(?:;" PARAM ")*)"
#define PARAM PCHAR "*"
#define PCHAR "(?:[" UNRESERVED ":@&=\\+\\$,]|" ESCAPED ")"

#define QUERY URIC "*"

#define FRAGMENT URIC "*"


#define URIC "(?:[" RESERVED UNRESERVED "]|" ESCAPED ")"
#define RESERVED ";\\/\\?:@&=\\+\\$,"
#define UNRESERVED ALPHANUM MARK
#define MARK "-_\\.!~\\*'\\(\\)"

#define ESCAPED "(?:%%[" HEX "][" HEX "])"
#define HEX DIGIT "A-Fa-f"

#define ALPHANUM ALPHA DIGIT
#define ALPHA LOWALPHA UPALPHA

#define LOWALPHA "a-z"
#define UPALPHA "A-Z"
#define DIGIT "0-9"


#define METHOD_OPTS \
    "OPTIONS | "    \
    "GET | "        \
    "HEAD | "       \
    "POST | "       \
    "PUT | "        \
    "DELETE | "     \
    "TRACE | "      \
    "CONNECT"


static regex_t header;

int http_init() {
#define REGCOMP(reg_struct_ptr, reg_str)    \
    if (regcomp(reg_struct_ptr, reg_str, CFLAGS) != 0) {  \
        fprintf(stderr, "Could not compile regex \"" reg_str "\"\n");   \
        return 1;   \
    }
    //REGCOMP(&header, HEADER);

#undef REGCOMP

#define DO(macro) \
    printf(macro "\n\n");

    printf("uri:\n" URI "\n\n");
    exit(0);
    return 0;
}

void http_exit() {
    regfree(&header);
}





int http_parse(struct http *p, dmsg_list *req) {
    char buf[MAX_LINE];

    while (dmsg_getline(req, buf, sizeof(buf)) > 0) {
        switch (p->state) {
        case METHOD:

            break;
        case HEADERS:
            break;
        case BODY:
            break;
        }
    }

    return 0;
}


int http_respond(struct http *p, int fd) {
    return 0;
}

