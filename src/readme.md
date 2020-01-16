# High-Performance Network Server

The core concept behind the internal mechanics of this server is multiplexed reading and writing parallelized across many
threads. By default, on initialization, the process spawns one thread for each locical processors on the machine, and each
of the threads is bound to a different CPU (except on OSX, where each thread is given a unique CPU affinity set ID). Then,
each thread blocks on an ``epoll`` (Linux) or ``kqueue`` (OSX) call, which waits for a write event to occur on the socket
file descriptor used to accept new connections. After a thread accepts the connection, a small amount of memory is allocated
to record data from the connection and track the client's state, the file descriptor associated with the socket stream is
placed in the appropriate event queueing mechanism, and the thread returns to wait for a new event to occur.



### Recording client data (``dmsg.c``)

Data read from a socket is written directly to a dynamic geometrically-growing set of scattered buffers implemented in
``dmsg.c`` (dynamic messages). At initialization, just one buffer is allocated (default size is 64 bytes). Once the buffer
has been filled, a second buffer twice as large is allocated. After the second has filled too, a third twice as large as the
second is allocated, and so on. The memory layout of the scattered buffers matches that of ``iovec``s, meaning ``dmsg`` can
make more efficient writes with the ``writev`` syscall.

The design of the dynamic message buffers allows for essentially arbitrarily large amounts of data to be received by a single
connection without needing to do any ``realloc``cing or copying, and the total amount of allocated space is never more than
twice optimal for messages at least 32 bytes in size, excluding the overhead required to store the scattered buffers.


### HTTP Parsing

After each read from a socket connection, the read-in data is searched for CRLF (carriage-return + line-feed, or ``\r\n``),
indicating the end of a line, and for HTTP headers, the end of a request/option. The first line read in from the ``dmsg`` is
expected to be an HTTP request line, i.e. of the form

```abnf
    request-line = method ' ' request-uri ' ' http-version "\r\n"
```

The request methods are
```abnf
    method = "OPTIONS" | "GET" | "HEAD" | "POST" | "PUT" | "DELETE" | "TRACE" | "CONNECT"
```

As of right now, ``"GET"`` is the only method fully implemented.

The ``request-uri`` rule is very intricate, and the full BNF description of it can be found in
[grammars/http_header.bnf](https://github.com/ClaytonKnittel/Server/blob/master/grammars/http_header.bnf)

The only HTTP versions supported are 1.0 and 1.1, so http_version is simply
```abnf
    http-version = "HTTP/1.0" | "HTTP/1.1"
```


The options fields are subsequently parsed, but are much simpler to parse since their form is very simple. Below is the list
of supported options

```abnf
Connection: keep-alive | close | upgrade
```
```abnf
Upgrade: websocket
```


## Concurrency, Memory Management and Shutdown

The parallelization is implemented with Posix threads, and all of the threads share the same ``server`` struct from which to
read and track data from all connections, and they also share a single ``epoll`` or ``kqueue`` instance, which handles the
multiplexing of connection management.

#### Client Partitioning
No single event can be passed to two different threads from the ``epoll``/``kqueue`` syscall because with ``epoll``ing, ``EPOLLONESHOT`` is set with each client connection, disabling the file descriptor in the event queue until it is re-added
(which is done after the thread that pulled it out of the queue finishes its work on it), and with ``kqueue``, ``EV_DISPATCH`` is set, which disables the file descriptor in a similar way to ``EPOLLONESHOT``, needing to be re-enabled after work has
complete. In this way, no single connection can be processed by two separate threads, and thus no data races are possible
in the client structs themselves.

#### Socket Shutdown
If any write to a client socket fails with ``EPIPE``, the connection is immediately closed, the client's file descriptor is
removed from the event multiplexer, and all dynamically-allocated memory associated with the client is freed. If 0 bytes are
ever read from a socket, then the connection is closed similarly (but not on a read hangup, as there still may be some data
which has not yet been parsed). If a bad HTTP request is received, then the client is immediately transitioned into a response
state with an appropriate error level set (corresponding to an HTTP response code), and the response is sent.

#### Server Shutdown
To clean up all threads on server shutdown, a pipe is initialized upon server creation and added to the event queue. It is put
in level-triggered mode, thus when written to, all threads will eventually receive it from the event queue and gracefully exit
the main loop. After all threads have been joined back with the main thread (which is also one of the worker threads running
the main loop), all client connections are closed and associated data is freed, and then the server is shut down and all
remaining data is freed.

#### Connection Timeout
Every time a connection is written to or read from, the timeout of the connection is updated by the server to be some number
of seconds in the future (5 seconds by default), after which the connection is no longer guaranteed to be kept alive. There is
a periodic timer which goes off every so many seconds (5 by default), which triggers one of the threads to iterate from the
back of the list of client connections in the server and disconnect all which have expired. On Linux, this is implmemented
with a timer file, and on OSX, with the special ``EVFILT_TIMER`` construct in ``kqueue``.
