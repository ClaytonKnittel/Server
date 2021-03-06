#ifndef _HTTP_H
#define _HTTP_H

#include <string.h>

#include "dmsg.h"

#ifdef __APPLE__

// on Darwin, off_t is by default 64-bits
typedef off_t off64_t;

#endif


// error has occured
#define HTTP_ERR -1

// to be returned by http_parse when parsing has complete and the repsonse
// is ready to be sent
#define HTTP_DONE 0
// to be returned by http_parse when parsing has not complete, meaning the
// request likely has not been fully received
#define HTTP_NOT_DONE 1
// to be returned when the connection should be closed, either becase the
// response was complete and this is not keep-alive, or the read-end was closed
// by the client
#define HTTP_CLOSE 2
// to be returned when the connection should not be closed, likely because
// the client requested the connection to be kept alive
#define HTTP_KEEP_ALIVE 3


/* states of the http request FSM */

// awaiting the initial request line, which gives the method, identifier, and
// protocol version of the requested resource
#define REQUEST 0

// reading headers
#define HEADERS 1

// reading in the body of the message
#define BODY 2

// finished reading message, ready to write response headers into client's log
#define RESPONSE 3

// currently in the process of sending requested file
#define SENDING_FILE 4


/* response status-codes */

// 39 states, requre 6 bits
enum status {
    none = 0,
    cont,
    switch_prot,
    ok,
    created,
    accepted,
    non_auth_info,
    no_content,
    reset_content,
    partial_content,
    multiple_choices,
    moved_permanently,
    found,
    see_other,
    not_modified,
    use_proxy,
    temporary_redirect,
    bad_request,
    unauthorized,
    payment_required,
    forbidden,
    not_found,
    method_not_allowed,
    not_acceptable,
    proxy_auth_req,
    request_timeout,
    conflict,
    gone,
    length_req,
    precondition_failed,
    req_entity_too_large,
    req_uri_too_large,
    unsupported_media_type,
    req_range_not_satisfiable,
    expectation_failed,
    internal_server_err,
    not_implemented,
    bad_gateway,
    service_unavailable,
    gateway_timeout,
    http_version_not_supported
};


/* flag values */

// http version in use
#define HTTP_1_0 0x0
#define HTTP_1_1 0x1

// keep-alive
#define KEEP_ALIVE 0x80000

// method
#define OPTIONS 0x00
#define GET     0x10
#define HEAD    0x20
#define POST    0x30
#define PUT     0x40
#define DELETE  0x50
#define TRACE   0x60
#define CONNECT 0x70
#define INVALID 0xf0


// number of bits taken by MIME type
#define MIME_TYPE_BITS   5
// offset in status bitvector
#define MIME_TYPE_OFFSET 14

struct http {
    /*
     * bitpacking all states in status variable:
     *  V - HTTP version
     *  F - state (of FSM)
     *  M - method
     *  S - status
     *  T - MIME type of requested file
     *  A - keep-alive (1 = yes, 0 = no)
     *
     * | msb                         lsb |
     * ________ ____ATTT TTSSSSSS MMMMFFFV
     *
     */
    int status;

    // fd to open requested file. If no file was requested, value is -1,
    // if the entire request has not yet been received or an error occured,
    // its value is undefined and should not be read
    int fd;

    // the size of the requested file, in bytes
    off64_t file_size;

    // number of bytes of data that have already been transmitted across the
    // connection
    off64_t offset;
};

/*
 * to be called once per process, initializes all global data used by the http
 * parser
 */
int http_init();

/*
 * to be called once per process, inverse of http_init
 */
void http_exit();



static __inline void http_clear(struct http *h) {
    h->status = 0;
    h->fd = -1;
}

/*
 * deallocates/closes any memory or files being used by this http struct and
 * resets the state of it. It must be called on an initialized http struct
 * (for example one that has been cleared with http_clear)
 */
void http_close(struct http *h);

/*
 * parses an http request, storing necessary metadata about the request in
 * the http struct
 *
 * return values:
 *  HTTP_DONE: the request has been fully parsed and is a complete request,
 *      so http_response should be called. This is returned even when an error
 *      has been detected, as the next step for the server is to respond with
 *      the appropriate error message
 *  HTTP_NOT_DONE: the request was incomplete, so this needs to be called again
 *      after more data has been received from the client
 */
int http_parse(struct http *p, dmsg_list *req);

/*
 * writes an appropriate response to the socket file descriptor provided
 *
 * return values:
 *  0 on success
 *  -1 on failure (i.e. socket closed)
 */
int http_respond(struct http *p, int fd);


/*
 * display the http object formatted, for debugging purposes
 */
void http_print(struct http *p);

#endif /* _HTTP_H */
