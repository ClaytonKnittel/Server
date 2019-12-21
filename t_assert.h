#ifdef VERBOSE
#define v_ensure(...) __VA_ARGS__
#else
#define v_ensure(...)
#endif


#define announce(...) { \
    v_ensure(fprintf(stderr, P_CYAN "Run:\t" P_RESET #__VA_ARGS__ "\n")); \
    __VA_ARGS__; \
}

#define assert(actual, expect) { \
    _assert((actual), (expect), P_YELLOW "Assert:\t" P_RESET #actual, __LINE__); \
}

#define assert_neq(actual, expect) { \
    _assert_neq((actual), (expect), P_YELLOW "Assert:\t" P_RESET #actual, __LINE__); \
}

void _assert(int actual, int expect, const char msg[], int linenum);
void _assert_neq(int actual, int expect, const char msg[], int linenum);
void silence_stdout();

