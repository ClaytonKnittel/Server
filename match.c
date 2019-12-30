#include "match.h"


static char* _pattern_match(struct token *token, char *buf, int offset,
        size_t max_n_matches, size_t *n_matches, match_t matches[]);


static __inline char* _match_token_and(struct token *token, char *buf,
        int offset, size_t max_n_matches, size_t *n_matches,
        match_t matches[]) {

    size_t init_n_matches = *n_matches;
    c_pattern *patt = token->patt;
    char *endptr = buf;

    for (int token_n = 0; token_n < patt->token_count; token_n++) {
        endptr = _pattern_match(patt->token[token_n], endptr, offset
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
    c_pattern *patt = token->patt;

    for (int token_n = 0; token_n < patt->token_count; token_n++) {
        char *ret = _pattern_match(patt->token[token_n], buf, offset,
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


static char* _pattern_match(struct token *token, char *buf, int offset,
        size_t max_n_matches, size_t *n_matches, match_t matches[]) {

    size_t init_n_matches = *n_matches;
    // count number of times a pattern was found
    int match_count = 0;
    char *endptr = buf;

    if ((token->type & TOKEN_TYPE_PATTERN) != 0) {
        // this is a pattern
        c_pattern *patt = token->patt;

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
    }
    else {
        char_class *cc = token->cc;

        // continue matching characters until we reach the max or a mismatch
        // is found
        while ((token->max == -1 || match_count < token->max)
                && cc_is_match(cc, *endptr)) {
            match_count++;
            endptr++;
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

int pattern_match(c_pattern *patt, char *buf, size_t n_matches,
        match_t matches[]) {

    size_t capture_count;
    struct token token = {
        .patt = patt,
        .min = 1,
        .max = 1,
        .type = TOKEN_TYPE_PATTERN
    };
    char* ret = _pattern_match(&token, buf, 0, n_matches, &capture_count,
            matches);
    if (ret == NULL || *ret != '\0') {
        return MATCH_FAIL;
    }
    return 0;
}
