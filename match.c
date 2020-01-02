#include <stdlib.h>
#include <string.h>


#include "match.h"




static char* _pattern_match(struct token *token, char *buf, int offset,
        size_t max_n_matches, size_t *n_matches, match_t matches[]);


static __inline char* _match_token_and(struct token *token, char *buf,
        int offset, size_t max_n_matches, size_t *n_matches,
        match_t matches[]) {

    size_t init_n_matches = *n_matches;
    c_pattern *patt = &token->node->patt;
    char *endptr = buf;

    for (struct token *child = patt->first; child != NULL;
            child = child->next) {
        endptr = _pattern_match(child, endptr, offset
                + (int) (endptr - buf), max_n_matches, n_matches,
                matches);
        if (endptr == NULL) {
            // pattern did not match
            // reset n_matches in case captures were made in recursive call
            *n_matches = init_n_matches;
            return NULL;
        }
    }
    return endptr;
}

static __inline char* _match_token_or(struct token *token, char *buf,
        int offset, size_t max_n_matches, size_t *n_matches,
        match_t matches[]) {

    size_t init_n_matches = *n_matches;
    c_pattern *patt = &token->node->patt;

    for (struct token *child = patt->first; child != NULL;
            child = child->next) {
        char *ret = _pattern_match(child, buf, offset,
                max_n_matches, n_matches, matches);
        if (ret != NULL) {
            // we found a match
            return ret;
        }
        // reset matches count in case captures were made in recursive
        // call
        *n_matches = init_n_matches;
    }
    // no match was found
    return NULL;
}


/*
 * attempts to match as much of the given token as possible to the given
 * buffer, with offset being the index of the first character in buf in the
 * main string being matched, used only to calculate capturing group offsets
 *
 * returns either a pointer to the end of the region matched by token on
 * success or NULL on failure
 */
static char* _pattern_match(struct token *token, char *buf, int offset,
        size_t max_n_matches, size_t *n_matches, match_t matches[]) {

    size_t init_n_matches = *n_matches;
    // count number of times a pattern was found
    int match_count = 0;
    char *endptr = buf;
    c_pattern *patt;
    char_class *cc;
    literal *lit;

    int captures = (token->flags & TOKEN_CAPTURE) != 0;
    // if this token captures, we need to claim a spot in the match
    // buffer before making any recursive calls, as they may also capture
    *n_matches += captures;

    switch (token_type(token)) {
        case TYPE_PATTERN:
            // this is a pattern
            patt = &token->node->patt;

            while (token->max == -1 || match_count < token->max) {
                // look for a match to this pattern
                char *next;
                if (patt->join_type == PATTERN_MATCH_AND) {
                    next = _match_token_and(token, endptr, offset
                            + (int) (endptr - buf), max_n_matches, n_matches,
                            matches);
                }
                else {
                    next = _match_token_or(token, endptr, offset
                            + (int) (endptr - buf), max_n_matches, n_matches,
                            matches);
                }
                if (next != NULL) {
                    // pattern did match, so increment match count
                    match_count++;
                    endptr = next;
                }
                else {
                    // pattern did not match, so terminate loop
                    break;
                }
            }
            break;
        case TYPE_CC:
            cc = &token->node->cc;

            // continue matching characters until we reach the max or a mismatch
            // is found
            while ((token->max == -1 || match_count < token->max)
                    && cc_is_match(cc, *endptr)) {
                match_count++;
                endptr++;
            }
            break;
        case TYPE_LITERAL:
            lit = &token->node->lit;

            while ((token->max == -1 || match_count < token->max)
                    && memcmp(endptr, lit->word, lit->length) == 0) {
                match_count++;
                endptr += lit->length;
            }
            break;
        default:
            // invalid type
            *n_matches = init_n_matches;
            return NULL;
    }
    if (captures) {
        if (init_n_matches < max_n_matches) {
            matches[init_n_matches].so = offset;
            matches[init_n_matches].eo = offset + (int) (endptr - buf);
        }
    }

    if (token->min <= match_count) {
        // pattern did match
        return endptr;
    }
    else {
        // pattern did not match enough times
        *n_matches = init_n_matches;
        return NULL;
    }
}

int pattern_match(pattern_t *patt, char *buf, size_t n_matches,
        match_t matches[]) {

    size_t capture_count = 0;
    struct token token = {
        .node = patt,
        .min = 1,
        .max = 1,
        .flags = 0
    };
    char* ret = _pattern_match(&token, buf, 0, n_matches, &capture_count,
            matches);
    if (ret == NULL || *ret != '\0') {
        return MATCH_FAIL;
    }
    if (capture_count > n_matches) {
        return MATCH_OVERFLOW;
    }
    // set the first unused match_t to have start offset of -1
    if (capture_count < n_matches) {
        matches[capture_count].so = -1;
    }
    return 0;
}


void pattern_free(pattern_t *patt) {
    patt_ref_dec(patt);
    if (patt_ref_count(patt) == 0) {
        if (patt_type(patt) == TYPE_PATTERN) {
            for (struct token *t = patt->patt.first; t != NULL; t = t->next) {
                pattern_free(t->node);
            }
        }
        free(patt);
    }
}

