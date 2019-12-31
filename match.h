#ifndef _MATCH_H
#define _MATCH_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * pattern matching follows augmented BNF standard
 */

// determine the type of the token
#define TYPE_MASK 0x3
#define TYPE_CC 0
#define TYPE_LITERAL 1
#define TYPE_PATTERN 2

#define PATT_ANONYMOUS 0x4

// first 3 bits are for type and anonymous flag, use remainder of flag for
// reference count
#define REF_COUNT_OFF 3

// flag set for tokens which capture
#define TOKEN_CAPTURE 0x1

#define PATTERN_MATCH_AND 1
#define PATTERN_MATCH_OR 2

#define MATCH_FAIL 1
#define MATCH_OVERFLOW 2

// char codes 0-255
#define NUM_CHARS 128

#define __bitv_t_shift 6
#define __bitv_t_size 64
#define __bitv_t uint64_t
#define __bitv_t_mask ((1 << __bitv_t_shift) - 1)



// generic pattern node which can match to things
typedef struct pattern_node {
    union {
        struct char_class *cc;
        struct literal *lit;
        struct c_pattern *patt;
    };
    // type is either:
    //  TYPE_CC: this is a char class
    //  TYPE_LITERAL: matches a literal string
    //  TYPE_PATTERN: this is a pattern
    // and may contain flags like
    //  PATT_ANONYMOUS: this pattern was not one of the named symbols
    //      during compilation of bnf
    // and the remainder of type is used for reference counting
    int type;
} pattern_t;


// for matching single characters to a set of characters
typedef struct char_class {
    unsigned long bitv[NUM_CHARS / (8 * sizeof(unsigned long))];
} char_class;

// for matching a string of characters exactly
typedef struct literal {
    char word[0];
} literal;



typedef struct c_pattern {
    // join type is one of
    //  PATTERN_MATCH_AND: each token must be found in sequence
    //  PATTERN_MATCH_OR: exactly one token is to be chosen, with
    //      precedence starting from the first token
    int join_type;

    struct {
        // singly-linked list of tokens which are all of the children
        // of this pattern
        struct token *first, *last;
    };
} c_pattern;



struct token {
    // node must be first member of token because of memory shortcut
    // used in augbnf.c to free tokens
    pattern_t node;

    // singly-linked list of tokens in a pattern
    struct token *next;

    // quantifier determines how to match the characters
    //
    // a max value of -1 denotes infinity
    //
    // example values:
    //  min = 0, max = -1 (match any number)
    //  min = 1, max = -1 (match at least one)
    //  min = 1, max = 1  (match exactly one)
    //  min = 0, max = 1  (optional)
    //
    struct {
        int min, max;
    };

    // can either be TOKEN_CAPTURE or not
    int flags;

};



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
int pattern_match(pattern_t *patt, char *buf, size_t n_matches,
        match_t matches[]);


static __inline void pattern_insert(c_pattern *patt, struct token *token) {
    if (patt->first == NULL) {
        patt->first = patt->last = token;
    }
    else {
        patt->last->next = token;
        patt->last = token;
        token->next = NULL;
    }
}


/*
 * recursively frees all children of this pattern, and then frees what is
 * pointed to by patt
 */
void pattern_free(pattern_t *patt);

/*
 * only frees anonymous symbols that are children of this pattern, but does
 * not free named symbols associated with it (used to free unused symbols
 * in bnf compilation)
 */
void pattern_free_shallow(pattern_t *patt);


// -------------------- pattern ops --------------------

static __inline int patt_anonymous(pattern_t *patt) {
    return (patt->type & PATT_ANONYMOUS) != 0;
}

static __inline int patt_type(pattern_t *patt) {
    return patt->type & TYPE_MASK;
}


// add to the reference count of pattern by amt
static __inline void patt_ref_add(pattern_t *patt, unsigned amt) {
    patt->type += (amt << REF_COUNT_OFF);
}

// increment reference count of pattern
static __inline void patt_ref_inc(pattern_t *patt) {
    patt->type += (1U << REF_COUNT_OFF);
}

// decrement reference count of pattern
static __inline void patt_ref_dec(pattern_t *patt) {
    patt->type -= (1U << REF_COUNT_OFF);
}

// gets the reference count of this pattern
static __inline unsigned patt_ref_count(pattern_t *patt) {
    return ((unsigned) patt->type) >> REF_COUNT_OFF;
}

static __inline int token_type(struct token *t) {
    return patt_type(&t->node);
}


// -------------------- char_class ops --------------------

static __inline void cc_clear(char_class *cc) {
    __builtin_memset(cc, 0, sizeof(char_class));
}

/*
 * determines whether the given char is in the character set
 */
static __inline int cc_is_match(char_class *cc, char c) {
    return (cc->bitv[c >> __bitv_t_shift] & (1LU << (c & __bitv_t_mask))) != 0;
}

/*
 * puts the given char in the character set
 */
static __inline void cc_allow(char_class *cc, char c) {
    cc->bitv[c >> __bitv_t_shift] |= (1LU << (c & __bitv_t_mask));
}

/*
 * removes the given char from the character set
 */
static __inline void cc_disallow(char_class *cc, char c) {
    cc->bitv[c >> __bitv_t_shift] &= ~(1LU << (c & __bitv_t_mask));
}

/*
 * adds all chars in other to cc
 */
static __inline void cc_allow_from(char_class *cc, char_class *other) {
    for (int i = 0; i < (sizeof(char_class) / sizeof(__bitv_t)); i++) {
        cc->bitv[i] |= other->bitv[i];
    }
}

/*
 * adds all characters within a range of ascii values to cc
 */
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

/*
 * adds all lowercase letters to cc
 */
static __inline void cc_allow_lower(char_class *cc) {
    cc_allow_range(cc, 'a', 'z');
}

/*
 * adds all upper case letters to cc
 */
static __inline void cc_allow_upper(char_class *cc) {
    cc_allow_range(cc, 'A', 'Z');
}

/*
 * adds all letters (lower and upper case) to cc
 */
static __inline void cc_allow_alpha(char_class *cc) {
    cc_allow_upper(cc);
    cc_allow_lower(cc);
}

/*
 * adds all digits to cc ('0' - '9')
 */
static __inline void cc_allow_num(char_class *cc) {
    cc_allow_range(cc, '0', '9');
}

/*
 * adds alpha chars (lower and upper case letters) and digits
 */
static __inline void cc_allow_alphanum(char_class *cc) {
    cc_allow_num(cc);
    cc_allow_alpha(cc);
}

/*
 * adds whitespace characters (horizontal tab, newline, vertical tab, form
 * feed, carriage return, and space)
 */
static __inline void cc_allow_whitespace(char_class *cc) {
    cc_allow_range(cc, '\t', '\r');
    cc_allow(cc, ' ');
}

/*
 * allows every character besides the null terminator (ascii value 0)
 */
static __inline void cc_allow_all(char_class *cc) {
    // do not allow '\0'
    cc_allow_range(cc, 1, NUM_CHARS - 1);
}

#endif /* _MATCH_H */
