#include <regex.h>
#include <stdlib.h>
#include <unistd.h>

#include "http.h"
#include "util.h"
#include "vprint.h"

// terminology from:
// https://www.w3.org/Protocols/HTTP/1.1/rfc2616bis/draft-lafon-rfc2616bis-03.html


#define CFLAGS 0

// size of buffer to hold each line from request
// (max method size (7)) + SP + URI + SP + (max version size (8)) + LF
#define MAX_LINE (8 + MAX_URI_SIZE + 10)
// maximum allowable size of URI
#define MAX_URI_SIZE 256



// ^(OPTIONS|GET|POST) (?:([^\s\?]+)(?:\?([^\s\?]*))?) (HTTP\/1.0|HTTP\/1.1)$


#define METHOD_OPTS \
    "OPTIONS|"      \
    "GET|"          \
    "HEAD|"         \
    "POST|"         \
    "PUT|"          \
    "DELETE|"       \
    "TRACE|"        \
    "CONNECT"


// relative group offsets in URI:
//  group 1: absolute URI heir part net path
//  group 2: absolute URI heir part abs path
//  group 3: absolute URI opaque part
//  group 4: relative URI net path
//  group 5: relative URI abs path
//  group 6: relative URI rel path

typedef struct {
    regmatch_t abs_heir_net;
    regmatch_t abs_heir_abs;
    regmatch_t abs_opaque;
    regmatch_t rel_net;
    regmatch_t rel_abs;
    regmatch_t rel_rel;
} uri_match;

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

#define SCHEME "(?:[" ALPHA "][" ALPHA DIGIT "\\+\\-\\.]*)"

#define AUTHORITY "(?:" SERVER "|" REG_NAME ")"

#define REG_NAME "(?:[" UNRESERVED "\\$,;:@&=\\+]|" ESCAPED ")+"

#define SERVER "(?:(?:" USERINFO "@)?" HOSTPORT ")?"
#define USERINFO "(?:[" UNRESERVED ";:&=\\+\\$,]|" ESCAPED ")*"

#define HOSTPORT "(?:" HOST "(?::" PORT ")?)"
#define HOST "(?:" HOSTNAME "|" IPV4ADDRESS ")"
#define HOSTNAME "(?:(?:" DOMAINLABEL "\\.)*" TOPLABEL "\\.?)"
#define DOMAINLABEL "(?:[" ALPHANUM "](?:[" ALPHANUM "\\-]+[" ALPHANUM "])?)"
#define TOPLABEL "(?:[" ALPHA "](?:[" ALPHANUM "\\-]+[" ALPHANUM "])?)"
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


#define URIC "(?:[" UNRESERVED RESERVED "]|" ESCAPED ")"
#define RESERVED ";\\/\\?:@&=\\+\\$,"
#define UNRESERVED ALPHANUM MARK
#define MARK "\\-_\\.!~\\*'\\(\\)"

#define ESCAPED "(?:%%[" HEX "][" HEX "])"
#define HEX DIGIT "A-Fa-f"

#define ALPHANUM ALPHA DIGIT
#define ALPHA LOWALPHA UPALPHA

#define LOWALPHA "a-z"
#define UPALPHA "A-Z"
#define DIGIT "0-9"



// group offsets:
//  group 1: method
//  groups 2-7: uri
//  group 8: http version

typedef struct {
    regmatch_t method;
    uri_match uri;
    regmatch_t http_v;
} header_match;

#define HEADER "^(" METHOD_OPTS ") " URI " (HTTP\\/1.0|HTTP\\/1.1)$"


static regex_t header;

static void reg_err(int ret) {
    printf(P_RED);
    switch (ret) {
    case REG_BADBR:
        printf("Invalid use of back reference operator\n");
        break;
    case REG_BADPAT:
        printf("Invalid use of pattern operators such as group or list\n");
        break;
    case REG_BADRPT:
        printf("Invalid use of repitition operators such as using '*' as the"
                "first character\n");
        break;
    case REG_EBRACE:
        printf("Un-matched brace interval operators\n");
        break;
    case REG_EBRACK:
        printf("Un-matched bracket list operators\n");
        break;
    case REG_ECOLLATE:
        printf("Invalid collating element\n");
        break;
    case REG_ECTYPE:
        printf("Unknown character class name\n");
        break;
    case REG_EESCAPE:
        printf("Trailing backslash\n");
        break;
    case REG_EPAREN:
        printf("Un-matched parenthesis group operators\n");
        break;
    case REG_ERANGE:
        printf("Invalid use of the range operator\n");
        break;
    case REG_ESPACE:
        printf("The regex routines ran out of memory\n");
        break;
    case REG_ESUBREG:
        printf("Invalid back reference to a subexpression\n");
        break;
    case 0:
        printf("No error\n");
        break;
    default:
        printf("Bad error code %d\n", ret);
        break;
    }
    printf(P_RESET);
}

int http_init() {
    int ret;
#define REGCOMP(reg_struct_ptr, reg_str)    \
    if ((ret = regcomp(reg_struct_ptr, reg_str, CFLAGS)) != 0) {  \
        fprintf(stderr, "Could not compile regex (%d) \"" reg_str "\"\n",   \
                ret);   \
        reg_err(ret);   \
        return 1;   \
    }
    REGCOMP(&header, HEADER);

#undef REGCOMP

    return 0;
}

void http_exit() {
    regfree(&header);
}




/*
 * sets http version flag in http struct, if http_minor is 1, that represents
 * HTTP/1.1, and if http_minor is 0, that represents HTTP/1.0
 */
static __inline void set_version(struct http *p, char http_minor) {
    char *b = (char*) &p->status;
    *b = http_minor | (0xfe & *b);
}

static __inline char get_version(struct http *p) {
    char *b = (char*) &p->status;
    return 0x01 & *b;
}

/*
 * sets state of the http struct, legal values are REQUEST, HEADERS, BODY,
 * and RESPONSE
 */
static __inline void set_state(struct http *p, char state) {
    char *b = (char*) &p->status;
    *b = (state << 2) | (0xf3 & *b);
}

static __inline char get_state(struct http *p) {
    char *b = (char*) &p->status;
    return (0x0c & *b) >> 2;
}

/*
 * sets the status-code of the http struct for the response, legal values
 * are any enum state
 */
static __inline void set_status(struct http *p, char status) {
    char *b = ((char*) &p->status) + 1;
    *b = status | (0xc0 & *b);
}

static __inline char get_status(struct http *p) {
    char *b = ((char*) &p->status) + 1;
    return 0x3f & *b;
}




static __inline void parse_method(struct http *p, char *buf, regmatch_t match) {
    // all of the first characters are different, except for POST and PUT
    switch (buf[match.rm_so]) {
    case 'O': // OPTIONS
        set_version(p, OPTIONS);
        break;
    case 'G':
        set_version(p, GET);
        break;
    case 'H':
        set_version(p, HEAD);
        break;
    case 'P':
        set_version(p, (buf[match.rm_so + 1] == 'O' ? POST : PUT));
        break;
    case 'D':
        set_version(p, DELETE);
        break;
    case 'T':
        set_version(p, TRACE);
        break;
    case 'C':
        set_version(p, CONNECT);
        break;
    }
}

static __inline void parse_version(struct http *p, char *buf, regmatch_t match) {
    // in a match, really only the last character differentiates the version,
    // between HTTP/1.0 and HTTP/1.1
    set_version(buf[match.rm_eo - 1] - '0');
}


int http_parse(struct http *p, dmsg_list *req) {
    char req_file_path[MAX_URI_SIZE];
    char buf[MAX_LINE];
    struct {
        regmatch_t whole;
        union {
            header_match header;
        };
    } match;

    char state = get_state(p);

    while (dmsg_getline(req, buf, sizeof(buf)) > 0) {
        switch (state) {
        case REQUEST:
            if (errno == DMSG_PARTIAL_READ) {
                // request header was too long
               set_state(p, RESPONSE);
               set_status(p, req_uri_too_large);
               return HTTP_ERR;
            }
            if (regexec(&header, buf, 1 + sizeof(header_match)
                        / sizeof(regmatch_t), (regmatch_t *) &match, 0)
                    == REG_NOMATCH) {
                // requets line not formatted properly
                set_state(p, RESPONSE);
                set_status(p, bad_request);
                return HTTP_ERR;
            }
            parse_method(p, buf, match.header.method);
            parse_version(p, buf, match.header.http_v);
            state = HEADERS;
            break;
        case HEADERS:
            break;
        case BODY:
            break;
        case RESPONSE:
            // should not have called parse if in response state
            return HTTP_ERR;
        }
    }
    set_state(p, state);

    return 0;
}


int http_respond(struct http *p, int fd) {
    if (get_state(p) != RESPONSE) {
        // should not call respond if not in respond state
        return HTTP_ERR;
    }
    return 0;
}

