#ifndef _MATCH_H
#define _MATCH_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>


// determine the type of the token
#define TOKEN_TYPE_CC 0
#define TOKEN_TYPE_PATTERN 1
// flag set for tokens which capture
#define TOKEN_CAPTURE 0x2

#define PATTERN_MATCH_AND 0
#define PATTERN_MATCH_OR 1

#define MATCH_FAIL 1
#define MATCH_OVERFLOW 2

// char codes 0-255
#define NUM_CHARS 128

#define __bitv_t_shift 6
#define __bitv_t_size 64
#define __bitv_t uint64_t
#define __bitv_t_mask ((1 << __bitv_t_shift) - 1)


// pattern matching follows augmented BNF standard


typedef struct {
    unsigned long bitv[NUM_CHARS / (8 * sizeof(unsigned long))];
} char_class;

struct token {
    union {
        char_class *cc;
        struct c_pattern *patt;
    };

    // quantifier determines how to match the characters
    //
    // a max value of -1 denotes infinity
    //
    // example values:
    //  min = 0, max = -1 (match any number)
    //  min = 1, max = -1 (match at least one)
    //  min = 1, max = 1  (match exactly one)
    //
    struct {
        int min, max;
    };

    // type is either:
    //  TOKEN_TYPE_CC: this is a char class
    //  TOKEN_TYPE_PATTERN: this is a pattern
    // and can contain the flags
    //  TOKEN_CAPTURE
    int type;
};


typedef struct c_pattern {
    // join type is one of
    //  PATTERN_MATCH_AND: each token must be found in sequence
    //  PATTERN_MATCH_OR: exactly one token is to be chosen, with
    //      precedence starting from the first token
    int join_type;

    // token count is the number of tokens in this level of the pattern
    int token_count;
    struct token *token[0];
} c_pattern;


typedef struct {
    // start offset and end offset of a found match. End offset is the index
    // after the last location in the found match
    ssize_t so, eo;
} match_t;



// -------------------- pattern matching ops -------------------

/*
 * attempts to match the supplied string to the given pattern. A token will
 * continue matching until a failing condition is met, after which it either
 * proceeds to the next token (PATTERN_MATCH_AND join type) or returns
 * (PATTERN_MATCH_OR). If a token fails to match, it either returns a failed
 * match (PATTERN_MATCH_AND) or tries the next token (PATTERN_MATCH_OR)
 *
 * return values:
 *  0: success
 *  MATCH_FAIL: no match found
 *  MATCH_OVERFLOW: more capturing groups were found than n_matches
 */
int pattern_match(c_pattern *patt, char *buf, size_t n_matches,
        match_t matches[]);



// -------------------- char_class ops --------------------

static __inline void cc_clear(char_class *cc) {
    __builtin_memset(cc, 0, sizeof(char_class));
}

static __inline int cc_is_match(char_class *cc, char c) {
    return (cc->bitv[c >> __bitv_t_shift] & (1LU << (c & __bitv_t_mask))) != 0;
}

static __inline void cc_allow(char_class *cc, char c) {
    cc->bitv[c >> __bitv_t_shift] |= (1LU << (c & __bitv_t_mask));
}

static __inline void cc_disallow(char_class *cc, char c) {
    cc->bitv[c >> __bitv_t_shift] &= ~(1LU << (c & __bitv_t_mask));
}

static __inline void cc_allow_from(char_class *cc, char_class *other) {
    for (int i = 0; i < (sizeof(char_class) / sizeof(__bitv_t)); i++) {
        cc->bitv[i] |= other->bitv[i];
    }
}

static __inline void cc_allow_range(char_class *cc, char l, char h) {
    int start_loc = l >> __bitv_t_shift;
    int end_loc = h >> __bitv_t_shift;
    l &= __bitv_t_mask;
    h &= __bitv_t_mask;

    if (start_loc == end_loc) {
        cc->bitv[start_loc] |= (((1LU << h) << 1) - 1) & ~((1LU << l) - 1);
    }
    else {
        cc->bitv[start_loc] |= ~((1LU << l) - 1);
        while (++start_loc < end_loc) {
            cc->bitv[start_loc] = ~0LU;
        }
        cc->bitv[start_loc] |= ((1LU << h) << 1) - 1;
    }
}

static __inline void cc_allow_lower(char_class *cc) {
    cc_allow_range(cc, 'a', 'z');
}

static __inline void cc_allow_upper(char_class *cc) {
    cc_allow_range(cc, 'A', 'Z');
}

static __inline void cc_allow_alpha(char_class *cc) {
    cc_allow_upper(cc);
    cc_allow_lower(cc);
}

static __inline void cc_allow_num(char_class *cc) {
    cc_allow_range(cc, '0', '9');
}

static __inline void cc_allow_alphanum(char_class *cc) {
    cc_allow_num(cc);
    cc_allow_alpha(cc);
}

static __inline void cc_allow_all(char_class *cc) {
    // do not allow '\0'
    cc_allow_range(cc, 1, NUM_CHARS - 1);
}

#endif /* _MATCH_H */