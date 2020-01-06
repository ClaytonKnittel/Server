#ifndef _MATCH_H
#define _MATCH_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * Pattern matching follows augmented BNF standard
 *
 *
 * Patterns are implemented as a finite state machine made up of tokens and
 *  patterns. Tokens define ways of moving throughout the machine, and patterns
 *  give constrains on when actions are allowed to be made on a token.
 *
 *
 *
 * Patterns can be one of three types:
 *
 *  Literal: just a string, i.e. "Hello"
 *
 *  Character Class: a set of characters with ASCII codes between 1-127
 *
 *  Token: points to a token, which is expected to lead back to this token
 *      on all paths that do not fail
 *
 *
 *
 * Tokens contain the following information pertinent to pattern matching:
 *
 *  node: the pattern associated with this token
 *
 *  min, max: the minimum and maximum number of times this token may be
 *      consumed in matching before moving on to the following token (next)
 *
 *  next: the subsequent token to be processed after sucessful consumption of
 *      this token
 *
 *  alt: an alternative token to consume in place of this token if successful
 *      completion of pattern matching is found to not be possible following
 *      this token
 *
 *
 * 
 * Consumption rules:
 *
 *  A literal can be consumed on an input buffer (string) if the string exactly
 *      matches the beginning of the buffer, and upon consumption the literal
 *      advances the buffer to the end of the matched string
 *
 *  A character class can be consumed on an input buffer if the first character
 *      in the buffer is in the character class, and upon consumption the
 *      character class advances the buffer one location
 *
 *  A token is consumed by consuming whatever its node points to on the current
 *      state of the input buffer
 *
 *
 *
 * FSM formation rules:
 *
 *  A pattern is represented by an FSM consisting of tokens, literals and
 *      character classes, which are all linked together by tokens.
 *
 *  No token may be referenced by a token which lies ahead of it along any
 *      path of next's and alt's, i.e. the graph must be acyclic along edges
 *      represented by the next and alt fields of the tokens
 *
 *  Tokens which contain other tokens (i.e. a token whose node is also a token)
 *      require that that child token lead back to the parent on all matching
 *      paths (following any of next, alt, and node fields of tokens), and that
 *      no paths lead to termination (i.e. next=NULL)
 *
 *  If a token is an alt of some other token, then it cannot be referenced by
 *      any other token (as a next, alt, or node), i.e. its reference count
 *      must be exactly 1
 *
 *  No token may have a node value of NULL
 *
 *
 *
 * FSM iteration rules:
 *
 *  A token may follow its next pointer if and only if it has been adjacently
 *      consumed at least "min" number of times and at most "max" times
 *
 *  A token may follow its alt pointer if and only if it has not been consumed
 *
 *  A match is found on an input buffer if some path through the FSM entirely
 *      consumes the buffer and ends on a NULL next node, i.e. the last token
 *      to consume the buffer to completion has a next value of NULL
 *
 */

// determine the type of the token
#define TYPE_MASK 0x3
#define TYPE_CC 0
#define TYPE_LITERAL 1
// always matches nothing successfully
#define TYPE_TOKEN 2

// first 3 bits are for type and anonymous flag, use remainder of flag for
// reference count
#define REF_COUNT_OFF 3
#define REF_COUNT_MASK ((1U << REF_COUNT_OFF) - 1)

// flag set for tokens which capture
#define TOKEN_CAPTURE 0x4

// failure codes
#define MATCH_FAIL 1

// char codes 0-255
#define NUM_CHARS 128

#define __bitv_t_shift 6
#define __bitv_t_size 64
#define __bitv_t uint64_t
#define __bitv_t_mask ((1 << __bitv_t_shift) - 1)


#define CPATT_TO_PATT(cpatt) ((pattern_t*) (cpatt))

// for matching single characters to a set of characters
typedef struct char_class {
    // to shadow type in pattern_t
    int type;

    unsigned long bitv[NUM_CHARS / (8 * sizeof(unsigned long))];
} char_class;

// for matching a string of characters exactly
typedef struct literal {
    // to shadow type in pattern_t
    int type;

    // length of word, excluding the null-terminator
    unsigned length;

    char word[0];
} literal;



typedef struct token {
    // can either be TOKEN_CAPTURE or not (also is used by pattern_t for type)
    int flags;

    // lowest 3 bits can be used by algorithms for temporary data storage, but
    // should always be 0 outside any method call
    int tmp;

    // contents of the token to be matched against
    // if node is null, then this token is a pass-through (noop)
    union pattern_node *node;

    // singly-linked list of tokens in a layer, all of which are
    // possible options for matching a given pattern
    struct token *alt;

    // pointer to token(s) which must follow this token if it is
    // selected
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

    // only defined in capturing groups, indicates the index into the matches
    // list that this token should write to. It is defined as the order in
    // which the capturing tokens are parsed, i.e. the order they are written
    // in the bnf expression
    unsigned match_idx;

} token_t;


// generic pattern node which can match to things
typedef union pattern_node {
    // type is either:
    //  TYPE_CC: this is a char class
    //  TYPE_LITERAL: matches a literal string
    // and may contain flags like
    //  PATT_ANONYMOUS: this pattern was not one of the named symbols
    //      during compilation of bnf
    // and the remainder of type is used for reference counting
    int type;

    char_class cc;
    literal lit;
    token_t token;
} pattern_t;



typedef struct {
    // start offset and end offset of a found match. End offset is the index
    // after the last location in the found match
    ssize_t so, eo;
} match_t;



// -------------------- pattern struct construction --------------------


/*
 * mallocs a literal* and initailizes all fields besides the word itself.
 * word_length is the length of the word to be stored, which does not include
 * the null terminator
 */
static __inline pattern_t* make_literal(unsigned word_length) {
    pattern_t *patt = (pattern_t*) malloc(sizeof(literal) + word_length);
    if (patt != NULL) {
        patt->type = TYPE_LITERAL;
        patt->lit.length = word_length;
    }
    return patt;
}

// forward declare for this constructor
static __inline void cc_clear(char_class*);

static __inline pattern_t* make_char_class() {
    pattern_t *patt = (pattern_t*) malloc(sizeof(char_class));
    if (patt != NULL) {
        patt->type = TYPE_CC;
        cc_clear(&patt->cc);
    }
    return patt;
}

#include <stdio.h>
static __inline pattern_t* make_token() {
    pattern_t *t = (pattern_t *) calloc(1, sizeof(token_t)/* - 8*/);
    t->type = TYPE_TOKEN;
    return t;
}

static __inline pattern_t* make_capturing_token() {
    pattern_t *t = (pattern_t *) calloc(1, sizeof(token_t));
    t->type = TYPE_TOKEN | TOKEN_CAPTURE;
    return t;
}


/*
 * makes a deep copy of the token (but not of the pattern_t types other than
 * token_t referenced by it)
 */
token_t* pattern_deep_copy(token_t*);



// -------------------- pattern matching ops -------------------

/*
 * attempts to match the supplied string to the given pattern. A token will
 * continue matching until a failing condition is met, after which it
 * backtracks and tries matching to alternatives (or's in bnf form)
 *
 * return values:
 *  0: success
 *  MATCH_FAIL: no match found
 *  MATCH_OVERFLOW: more capturing groups were found than n_matches
 */
int pattern_match(token_t *patt, char *buf, size_t n_matches,
        match_t matches[]);



/*
 * recursively frees the entire pattern structure, following all links
 * and safely freeing resources pointed to by the tokens
 */
void pattern_free(token_t *patt);


typedef struct size_info {
    size_t n_patterns;
    size_t n_tokens;
} size_info_t;

/*
 * calculates the number of tokens and patterns in this pattern structure,
 * returning the result as a (n_patterns, n_tokens) pair
 */
size_info_t pattern_size(token_t *patt);


/*
 * connects patt to pattern "to", meaning each node in patt with a
 * next pointer to NULL is set to point to "to". This is effectively
 * saying that rule "patt" must be followed by rule "to"
 *
 * return 0 on success and -1 if nothing was able to be connected
 */
int pattern_connect(token_t *patt, token_t *to);


/*
 * connects patt to pattern "opt" conditionally, so either patt may be chosen
 * or opt may be
 */
int pattern_or(token_t *patt, token_t *opt);



// -------------------- pattern ops --------------------

// returns the type of this pattern
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

// gets the type of the pattern wrapped by this token
static __inline int token_type(token_t *t) {
    return patt_type(t->node);
}

// gives whether or not this token capture
static __inline int token_captures(token_t *t) {
    return (t->flags & TOKEN_CAPTURE) != 0;
}


// -------------------- char_class ops --------------------

static __inline void cc_clear(char_class *cc) {
    cc->type = TYPE_CC;
    __builtin_memset(&cc->bitv[0], 0, sizeof(char_class) -
            offsetof(char_class, bitv));
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
