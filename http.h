

struct http {

};

int http_init();

void http_exit();

int http_parse(struct http *p, const char* req);

