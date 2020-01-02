#include <stdlib.h>
#include <string.h>


#include "match.h"


/*
 * attempts to match as much of the given token as possible to the given
 * buffer, with offset being the index of the first character in buf in the
 * main string being matched, used only to calculate capturing group offsets
 *
 * returns number of captures recorded on success (found a match) or -1 on failure
 *
 * TODO pass in min + max (so loops in loops can work) and capturing (so don't
 * capture one group multiple times)
 */
static int _pattern_match(token_t *patt, char *buf, int offset,
        size_t max_n_matches, size_t n_matches, match_t matches[]) {

    if (patt == NULL) {
        return (*buf == '\0') ? 0 : -1;
    }

    char *endptr = buf;
    int captures;
    // count number of times a pattern was found
    char_class *cc;
    literal *lit;
    token_t *token;

    captures = token_captures(patt);

    int ret = -1;

    int min = patt->min;
    int max = patt->max;
    switch (token_type(patt)) {
        case TYPE_CC:
            cc = &patt->node->cc;

            if (cc_is_match(cc, *endptr)) {
                endptr++;
            }
            break;
        case TYPE_LITERAL:
            lit = &patt->node->lit;

            if (memcmp(endptr, lit->word, lit->length) == 0) {
                endptr += lit->length;
            }
            break;
        case TYPE_TOKEN:
            token = &patt->node->token;

            if (max > 0) {
                // then we can safely repeat this op again
                ret = _pattern_match(token->next, endptr, offset,
                        max_n_matches, n_matches + captures, matches);
            }
            if (ret == -1 && min == 0) {
                // then we are able to try moving on to the next token
                ret = _pattern_match(token->alt, endptr, offset,
                        max_n_matches, n_matches + captures, matches);
            }
        default:
            // invalid type
            return -1;
    }

    if (buf != endptr) {
        // we made a match
        min = (min == 0) ? 0 : min - 1;
        max = (max == 0) ? 0 : (max == -1) ? -1 : max - 1;
    
        if (ret == -1 && max > 0) {
            ret = _pattern_match(patt, endptr, offset
                    + (int) (endptr - buf), max_n_matches, n_matches
                    + captures, matches);
        }
        
        if (ret == -1 && min == 0) {
            // if we used up as much as we needed, try moving on
            ret = _pattern_match(patt->next, endptr, offset
                    + (int) (endptr - buf), max_n_matches, n_matches
                    + captures, matches);
        }
    }

    if (ret == -1) {
        // if we could not make a match, then try an alternative
        ret = _pattern_match(patt->alt, buf, offset, max_n_matches,
                n_matches, matches);
    }

    if (ret >= 0 && captures) {
        if (n_matches < max_n_matches) {
            matches[n_matches].so = offset;
            matches[n_matches].eo = offset + (int) (endptr - buf);
        }
        // because this also captures
        ret++;
    }
    return ret;
}

int pattern_match(token_t *patt, char *buf, size_t n_matches,
        match_t matches[]) {

    int ret = _pattern_match(patt, buf, 0, n_matches, 0, matches);
    if (ret < 0) {
        return MATCH_FAIL;
    }
    if (ret > n_matches) {
        return MATCH_OVERFLOW;
    }
    // set the first unused match_t to have start offset of -1
    if (ret < n_matches) {
        matches[ret].so = -1;
    }
    return 0;
}


#define TOKEN_SEEN 0x8

static __inline void mark_seen(token_t *token) {
    token->flags |= TOKEN_SEEN;
}

static __inline void unmark_seen(token_t *token) {
    token->flags &= ~TOKEN_SEEN;
}

static __inline int is_seen(token_t *token) {
    return (token->flags & TOKEN_SEEN) != 0;
}

void pattern_free(token_t *token) {
    if (is_seen(token)) {
        return;
    }
    mark_seen(token);

    pattern_t *patt = token->node;
    patt_ref_dec(patt);
    if (patt_ref_count(patt) == 0) {
        free(patt);
    }
    if (token->alt != NULL) {
        pattern_free(token->alt);
    }
    if (token->next != NULL) {
        pattern_free(token->next);
    }
    free(token);
}


int pattern_connect(token_t *patt, token_t *to) {
    int ret = -1;

    if (patt->next == NULL) {
        // we can link patt to "to"
        patt->next = to;
        ret = 0;
    }
    else if (pattern_connect(patt->next, to) != -1) {
        ret = 0;
    }
    if (patt->alt != NULL) {
        if (pattern_connect(patt->alt, to) != -1) {
            ret = 0;
        }
    }
    return ret;
}

int pattern_or(token_t *patt, token_t *opt) {
    for (; patt->alt != NULL; patt = patt->alt);
    patt->alt = opt;
    return 0;
}

