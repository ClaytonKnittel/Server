#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/sendfile.h>
#elif __APPLE__
#include <sys/socket.h>
#endif
#include <sys/stat.h>

#include "augbnf.h"
#include "hashmap.h"
#include "http.h"
#include "match.h"
#include "util.h"
#include "vprint.h"


/* 
 * protocol reference page:
 * https://www.w3.org/Protocols/HTTP/1.1/rfc2616bis/draft-lafon-rfc2616bis-03.html
 */


// size of buffer to hold each line from request
// (max method size (7)) + SP + URI + SP + (max version size (8)) + LF
#define MAX_LINE (8 + MAX_URI_SIZE + 10)
// maximum allowable size of URI
#define MAX_URI_SIZE 256


//#if __USER__ dd
#define PUBLIC_FILE_SRC "/home/lilching/public"
// directory from which files are served
//#define PUBLIC_FILE_SRC __DIR__ "serveup"


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


// -------------------- static globals --------------------

static token_t *http_header;

struct http_header_match {
    // #tag, not important on our end
    match_t fragment;
    // usually 'http'
    match_t scheme;
    // normal uri
    match_t abs_uri;
    // uri without leading '/'
    match_t rel_uri;
    // username@website.com:port
    match_t authority;
    // proceed the '?', like var=val
    match_t query;
};


static hashmap extensions;


// align with indices in type vector of struct mime_type, 22 types, so need
// 5 bytes
enum {
    aac, arc, ostr, bmp, css, csv, gif, html, ico, ics, jpg, js, json, mp3,
    png, pdf, sh, tar, txt, xhtml, xml, zip, num_mime_types,
    default_mime_type = ostr
};
 
// struct used to store all mime types, that which applies to a given client
// is stored as a bit-compacted offset in the http struct associated with it
static const struct mime_types {
    char *type[num_mime_types];
} mime_type = {
    .type = {
        "audio/aac",
        "application/x-freearc",
        "application/octet-stream",
        "image/bmp",
        "text/css",
        "text/csv",
        "image/gif",
        "text/html",
        "image/vnd.microsoft.icon",
        "text/calendar",
        "image/jpeg",
        "text/javascript",
        "application/json",
        "audio/mpeg",
        "image/png",
        "application/pdf",
        "application/x-sh",
        "application/x-tar",
        "text/plain",
        "application/xhtml+xml",
        "application/xml",
        "application/zip"
    }
};

/*
 * extensions is a map from file extension to MIME type, so a requested file
 * can be given the write Content-Type header and displayed/used properly
 */
static void init_extensions() {
    static char
        aacs[]  = "aac",
        arcs[]  = "arc",
        bins[]  = "bin",
        bmps[]  = "bmp",
        csss[]  = "css",
        csvs[]  = "csv",
        gifs[]  = "gif",
        htmls[] = "html",
        icos[]  = "ico",
        icss[]  = "ics",
        jpgs[]  = "jpg",
        jpegs[] = "jpeg",
        jss[]   = "js",
        jsons[] = "json",
        mjss[]  = "mjs",
        mp3s[]  = "mp3",
        pngs[]  = "png",
        pdfs[]  = "pdf",
        shs[]   = "sh",
        tars[]  = "tar",
        txts[]  = "txt",
        xhtmls[]= "xhtml",
        xmls[]  = "xml",
        zips[]  = "zip";


    str_hash_init(&extensions);

#define off_get(field_name) \
    (offsetof(struct mime_types, field_name) / sizeof(char*))

    str_hash_insert(&extensions, aacs, (void*) aac);
    str_hash_insert(&extensions, arcs, (void*) arc);
    str_hash_insert(&extensions, bins, (void*) ostr);
    str_hash_insert(&extensions, bmps, (void*) bmp);
    str_hash_insert(&extensions, csss, (void*) css);
    str_hash_insert(&extensions, csvs, (void*) csv);
    str_hash_insert(&extensions, gifs, (void*) gif);
    str_hash_insert(&extensions, htmls, (void*) html);
    str_hash_insert(&extensions, icos, (void*) ico);
    str_hash_insert(&extensions, icss, (void*) ics);
    str_hash_insert(&extensions, jpgs, (void*) jpg);
    str_hash_insert(&extensions, jpegs, (void*) jpg);
    str_hash_insert(&extensions, jss, (void*) js);
    str_hash_insert(&extensions, jsons, (void*) json);
    str_hash_insert(&extensions, mjss, (void*) js);
    str_hash_insert(&extensions, mp3s, (void*) mp3);
    str_hash_insert(&extensions, pngs,(void*) png);
    str_hash_insert(&extensions, pdfs, (void*) pdf);
    str_hash_insert(&extensions, shs, (void*) sh);
    str_hash_insert(&extensions, tars, (void*) tar);
    str_hash_insert(&extensions, txts, (void*) txt);
    str_hash_insert(&extensions, xhtmls, (void*) xhtml);
    str_hash_insert(&extensions, xmls, (void*) xml);
    str_hash_insert(&extensions, zips, (void*) zip);

}



int http_init() {
    http_header = bnf_parsef("grammars/http_header.bnf");
    if (http_header == NULL) {
        fprintf(stderr, P_RED "http initialization failed\n" P_RESET);
        return -1;
    }
    init_extensions();

    return 0;
}

void http_exit() {
    pattern_free(http_header);
    hash_free(&extensions);
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
 * given a file extension, returns the index associated with the associated
 * MIME type, to be stored in the http struct
 */
static __inline void set_mime_type(struct http *p, const char* ext) {
    void* ret = str_hash_get(&extensions, ext);
    unsigned type;

    vprintf("mime type: %s\n", ext);

    if (ret == NULL) {
        // not a recognizes extension
        type = default_mime_type;
    }
    else {
        type = (size_t) ret;
    }

    p->status |= type << MIME_TYPE_OFFSET;
}

static __inline const char* get_mime_type(struct http *p) {
    unsigned type = (p->status >> MIME_TYPE_OFFSET) &
        ((1U << MIME_TYPE_BITS) - 1);

    return mime_type.type[type];
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


static __inline int parse_uri(struct http *p, char *buf) {

    struct http_header_match match;

    int ret = pattern_match(http_header, buf,
            sizeof(struct http_header_match) / sizeof(match_t),
            (match_t*) &match);

    if (ret == MATCH_FAIL) {
        // badly formatted uri
        p->fd = -1;
        return -1;
    }
    if (match.abs_uri.so == -1) {
        // no uri requested
        p->fd = -1;
        return -1;
    }

    char* uri = &buf[match.abs_uri.so];
    size_t uri_len = match.abs_uri.eo - match.abs_uri.so;

    // null-terminate uri, but save what was previously there so it can be
    // restored
    char tmp = buf[match.abs_uri.eo];
    buf[match.abs_uri.eo] = '\0';


    // first use URI to set MIME type in http struct
    char *ext = (char*) memchr(uri, '.', uri_len);
    if (ext == NULL) {
        // no extension, set ext to the end of uri, i.e. ""
        ext = uri + uri_len;
    }
    else {
        // skip the dot
        ext++;
    }
    set_mime_type(p, ext);


    char fullpath[sizeof(PUBLIC_FILE_SRC) + MAX_URI_SIZE];
    sprintf(fullpath, PUBLIC_FILE_SRC "%s", uri);

    // restore
    buf[match.abs_uri.eo] = tmp;

    p->fd = open(fullpath, O_RDONLY);

    vprintf("opened %s\n", fullpath);

    return 0;
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
            if (parse_uri(p, req_path) != 0) {
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
    //set_state(p, state);
    set_state(p, RESPONSE);
    set_status(p, ok);

    // FIXME
    return HTTP_DONE;
}


int http_respond(struct http *p, int fd) {
    if (get_state(p) != RESPONSE) {
        // should not call respond if not in respond state
        return HTTP_ERR;
    }
    //http_print(p);

    size_t size = 0;
    if (p->fd != -1) {
        struct stat stat;
        if (fstat(p->fd, &stat) == 0) {
            size = stat.st_size;
        }
        else {
            fprintf(stderr, "could not stat file, reason: %s", strerror(errno));
        }
    }

    char buf[4096];
    int len = snprintf(buf, sizeof(buf),
            "HTTP/1.1 %s\n"
            "Content-Length: %lu\n"
            "Content-Type: %s\n"
            "\n",
            get_status_str((unsigned) get_status(p)), size, get_mime_type(p));
    write(fd, buf, len);

    if (p->fd != -1) {
#ifdef __linux__
        sendfile(fd, p->fd, NULL, size);
#elif __APPLE__
        off_t n_bytes = size;
        sendfile(p->fd, fd, 0, &n_bytes, NULL, 0);
#endif
    }

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

