#include <errno.h>
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



static const char * const msgs[] = {
    "000 None",
    "100 Continue",
    "101 Switching Protocols",
    "200 OK",
    "201 Created",
    "202 Accepted",
    "203 Non-Authoritative Information",
    "204 No Content",
    "205 Reset Content",
    "206 Partial Content",
    "300 Multiple Choices",
    "301 Moved Permanently",
    "302 Found",
    "303 See Other",
    "304 Not Modified",
    "305 Use Proxy",
    "307 Temporary Redirect",
    "400 Bad Request",
    "401 Unauthorized",
    "402 Payment Required",
    "403 Forbidden",
    "404 Not Found",
    "405 Method Not Allowed",
    "406 Not Acceptable",
    "407 Proxy Authentication Required",
    "408 Request Time-Out",
    "409 Conflict",
    "410 Gone",
    "411 Length Required",
    "412 Precondition Failed",
    "413 Request Entity Too Large",
    "414 Request-URI Too Large",
    "415 Unsupported Media Type",
    "416 Requested Range Not Satisfiable",
    "417 Expectation Failed",
    "500 Internal Server Error",
    "501 Not Implemented",
    "502 Bad Gateway",
    "503 Service Unavailable",
    "504 Gateway Time-Out",
    "505 HTTP Version Not Supported"
};


static __inline const char* get_status_str(int status) {
    return msgs[status];
}


/*
 * writes the string error code and reason phrase corresponding to the given
 * status code
 *
 * the buffer must be at least 36 bytes long to fit all possible response
 * codes and messages
 */
static __inline void write_status_str(int status, int fd) {
    const char *msg = msgs[status];
    // TODO calculate at compile time
    int size = strlen(msg);
    write(fd, msg, size);
}


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

#define HEADER "^(" METHOD_OPTS ") " URI " (HTTP\\/1.0|HTTP\\/1.1)\\r?$"


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
    printf(HEADER "\n");

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
 * sets http request method type, legal values are GET, POST, etc.
 */
static __inline void set_method(struct http *p, char method) {
    char *b = (char*) &p->status;
    *b = method | (0x0f & *b);
}

static __inline char get_method(struct http *p) {
    char *b = (char*) &p->status;
    return 0xf0 & *b;
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
        set_method(p, OPTIONS);
        break;
    case 'G':
        set_method(p, GET);
        break;
    case 'H':
        set_method(p, HEAD);
        break;
    case 'P':
        set_method(p, (buf[match.rm_so + 1] == 'O' ? POST : PUT));
        break;
    case 'D':
        set_method(p, DELETE);
        break;
    case 'T':
        set_method(p, TRACE);
        break;
    case 'C':
        set_method(p, CONNECT);
        break;
    }
}

static __inline char* parse_uri(struct http *p, char *buf, uri_match uri) {
    regmatch_t m;
    if (uri.abs_heir_net.rm_so != -1) {
        // the uri given will match the file location we need to return
        m.rm_so = uri.abs_heir_net.rm_so;
        m.rm_eo = uri.abs_heir_net.rm_eo;
    }
    else if (uri.abs_heir_abs.rm_so != -1) {
        // the uri given will match the file location we need to return
        m.rm_so = uri.abs_heir_abs.rm_so;
        m.rm_eo = uri.abs_heir_abs.rm_eo;
    }
    else if (uri.abs_opaque.rm_so != -1) {
        // the uri given does not conform to our standards
        return NULL;
    }
    else if (uri.rel_net.rm_so != -1) {
        // the uri given will match the file location we need to return
        m.rm_so = uri.rel_net.rm_so;
        m.rm_eo = uri.rel_net.rm_eo;
    }
    else if (uri.rel_abs.rm_so != -1) {
        // the uri given will match the file location we need to return
        m.rm_so = uri.rel_abs.rm_so;
        m.rm_eo = uri.rel_abs.rm_eo;
    }
    else if (uri.rel_rel.rm_so != -1) {
        // the uri given will match the file location we need to return
        m.rm_so = uri.rel_rel.rm_so;
        m.rm_eo = uri.rel_rel.rm_eo;
    }
    buf[uri.abs_heir_net.rm_eo] = '\0';
    return buf + uri.abs_heir_net.rm_so;
}

static __inline void parse_version(struct http *p, char *buf, regmatch_t match) {
    // in a match, really only the last character differentiates the version,
    // between HTTP/1.0 and HTTP/1.1
    set_version(p, buf[match.rm_eo - 1] - '0');
}


int http_parse(struct http *p, dmsg_list *req) {
    char *req_path = NULL;
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
            req_path = parse_uri(p, buf, match.header.uri);
            if (req_path == NULL) {
                // the URI did not conform to our standards, i.e. we don't
                // like opaque URI's
                set_state(p, RESPONSE);
                set_status(p, not_found);
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

    // FIXME
    return HTTP_DONE;
}


int http_respond(struct http *p, int fd) {
    if (get_state(p) != RESPONSE) {
        // should not call respond if not in respond state
        return HTTP_ERR;
    }
    printf("http response!\n");
    http_print(p);
    return 0;
}



void http_print(struct http *p) {
    char *version, *method;

    switch (get_version(p)) {
    case HTTP_1_0:
        version = "HTTP/1.0";
        break;
    case HTTP_1_1:
        version = "HTTP/1.1";
        break;
    default:
        version = NULL;
        break;
    }

    switch (get_method(p)) {
    case OPTIONS:
        method = "OPTIONS";
        break;
    case GET:
        method = "GET";
        break;
    case HEAD:
        method = "HEAD";
        break;
    case POST:
        method = "POST";
        break;
    case PUT:
        method = "PUT";
        break;
    case DELETE:
        method = "DELETE";
        break;
    case TRACE:
        method = "TRACE";
        break;
    case CONNECT:
        method = "CONNECT";
        break;
    }

    printf("HTTP request:\nmethod: %s\nversion: %s\nresponse: %s\n",
            method, version, get_status_str(get_status(p)));
}

