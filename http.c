#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <unistd.h>

#include "http.h"
#include "match.h"
#include "util.h"
#include "vprint.h"

// terminology from:
// https://www.w3.org/Protocols/HTTP/1.1/rfc2616bis/draft-lafon-rfc2616bis-03.html


#define CFLAGS REG_EXTENDED

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


static const char * const method_opts[] = {
    "OPTIONS",
    "GET",
    "HEAD",
    "POST",
    "PUT",
    "DELETE",
    "TRACE",
    "CONNECT"
};



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


static struct patterns {
    char_class
        // digits '0' - '9'
        digit,
        // digit and 'a' - 'f' or 'A' - 'F'
        hex,
        // upper and lowercase letters 'a' - 'z' and 'A' - 'Z'
        alpha,
        // alpha and digit
        alphanum,
        // ;, /, ?, :, @, &, =, +, &, ","
        reserved,
        // alphanum and -, _, ., !, ~, *, ', (, )
        unreserved,
        // of the form % HEX HEX (need special treatment for this)
        escaped,
        // unreserved, reserved, or escaped
        uric;
} patterns;


static void init_patterns() {
    __builtin_memset(&patterns, 0, sizeof(struct patterns));

    cc_allow_num(&patterns.digit);

    cc_allow_num(&patterns.hex);
    cc_allow_range(&patterns.hex, 'A', 'F');
    cc_allow_range(&patterns.hex, 'a', 'f');

    cc_allow_alpha(&patterns.alpha);

    cc_allow_alphanum(&patterns.alphanum);

    cc_allow(&patterns.reserved, ';');
    cc_allow(&patterns.reserved, '/');
    cc_allow(&patterns.reserved, '?');
    cc_allow(&patterns.reserved, ':');
    cc_allow(&patterns.reserved, '@');
    cc_allow(&patterns.reserved, '&');
    cc_allow(&patterns.reserved, '=');
    cc_allow(&patterns.reserved, '+');
    cc_allow(&patterns.reserved, '$');
    cc_allow(&patterns.reserved, ',');

    cc_allow_alphanum(&patterns.unreserved);
    cc_allow(&patterns.unreserved, '-');
    cc_allow(&patterns.unreserved, '_');
    cc_allow(&patterns.unreserved, '.');
    cc_allow(&patterns.unreserved, '!');
    cc_allow(&patterns.unreserved, '~');
    cc_allow(&patterns.unreserved, '*');
    cc_allow(&patterns.unreserved, '\'');
    cc_allow(&patterns.unreserved, '(');
    cc_allow(&patterns.unreserved, ')');

    // must check that the subsequent two characters are hex
    cc_allow(&patterns.escaped, '%');

    cc_allow_from(&patterns.uric, &patterns.reserved);
    cc_allow_from(&patterns.uric, &patterns.unreserved);
    cc_allow_from(&patterns.uric, &patterns.escaped);
}



#define HEADER "^(" METHOD_OPTS ") " URI " (HTTP\\/1.0|HTTP\\/1.1)\\r?$"



int http_init() {
    init_patterns();

    return 0;
}

void http_exit() {
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




/*
 * parse method out of string and update state variable of the http struct
 *
 * returns 0 on success and -1 on failure
 */
static __inline int parse_method(struct http *p, char *method) {
    // all of the first characters are different, except for POST and PUT
    int idx;
    switch (method[0]) {
    case 'O':
        idx = OPTIONS;
        break;
    case 'G':
        idx = GET;
        break;
    case 'H':
        idx = HEAD;
        break;
    case 'P':
        idx = method[1] == 'O' ? POST : PUT;
        break;
    case 'D':
        idx = DELETE;
        break;
    case 'T':
        idx = TRACE;
        break;
    case 'C':
        idx = CONNECT;
        break;
    default:
        return -1;
    }
    // need to right shift by 4 because they occupy the upper 4
    // bits of a char
    if (strcmp(method, method_opts[idx >> 4]) != 0) {
        return -1;
    }
    set_method(p, idx);
    return 0;
}

static __inline char* parse_uri(struct http *p, char *buf) {
    /*if (uri.abs_heir_net.rm_so != -1) {
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
    buf[uri.abs_heir_net.rm_eo] = '\0';*/
    //return buf + uri.abs_heir_net.rm_so;
    return buf;
}

/*
 * parse HTTP version out of string, returning 0 on success and -1 on failure
 */
static __inline int parse_version(struct http *p, char *buf) {
    // in a match, really only the last character differentiates the version,
    // between HTTP/1.0 and HTTP/1.1
    if (strncmp(buf, "HTTP/1.", 7) != 0) {
        return -1;
    }
    if (buf[7] != '0' && buf[7] != '1') {
        return -1;
    }
    set_version(p, buf[7] - '0');
    return 0;
}


int http_parse(struct http *p, dmsg_list *req) {
    char *req_path = NULL;
    char *method, *version;
    char *tmp, buf[MAX_LINE];

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

#define TEST_BAD_FORMAT \
            if (buf == NULL) { \
                set_state(p, RESPONSE); \
                set_status(p, bad_request); \
                return HTTP_ERR; \
            }

            tmp = buf;
            method = tmp;

            tmp = strchr(method, ' ');
            TEST_BAD_FORMAT;
            *tmp = '\0';
            req_path = tmp + 1;

            tmp = strchr(req_path, ' ');
            TEST_BAD_FORMAT;
            *tmp = '\0';
            version = tmp + 1;

#undef TEST_BAD_FORMAT

            if (parse_method(p, method) != 0) {
                // bad request method
                set_state(p, RESPONSE);
                set_status(p, bad_request);
                return HTTP_ERR;
            }
            req_path = parse_uri(p, req_path);
            if (req_path == NULL) {
                // the URI was not properly formatted
                set_state(p, RESPONSE);
                set_status(p, not_found);
                return HTTP_ERR;
            }
            if (parse_version(p, version) != 0) {
                // requets line not formatted properly
                set_state(p, RESPONSE);
                set_status(p, bad_request);
                return HTTP_ERR;
            }
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
        printf("http response err!\n");
        http_print(p);
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

