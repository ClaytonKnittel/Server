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

