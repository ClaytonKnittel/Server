#include <stdlib.h>
#include <string.h>


#include "match.h"

// information returned by _pattern_match
typedef union ret_info {

    // -1 on error, something else otherwise
    long err;

    struct {
        // the number of captures that have been made
        int n_captures;
        // the size of the region captured by the specific token passed as a the
        // first parameter to this _pattern_match call
        unsigned capture_size;
    };
} ret_info_t;


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
static ret_info_t _pattern_match(token_t *patt, char *buf, int offset,
        size_t max_n_matches, size_t n_matches, match_t matches[]) {

    ret_info_t ret = { .err = -1 };

    if (patt == NULL) {
        if (*buf == '\0') {
            ret.n_captures = 0;
            ret.capture_size = 0;
        }
        return ret;
    }

    int captures = token_captures(patt);

    // count number of times a pattern was found
    literal *lit;

    // use the tmp field of the token to count number of uses
#define rep_count tmp

    int count = patt->rep_count;

    if (patt->max == -1 || patt->max > count) {
        // if we can match this pattern more, try to do so
        patt->rep_count++;

        switch (token_type(patt)) {
            case TYPE_CC:
                if (cc_is_match(&patt->node->cc, *buf)) {
                    ret = _pattern_match(patt, buf + 1, offset + 1,
                            max_n_matches, n_matches, matches);
                }
                break;
            case TYPE_LITERAL:
                lit = &patt->node->lit;
                if (strncmp(buf, lit->word, lit->length) == 0) {
                    ret = _pattern_match(patt, buf + lit->length,
                            offset + lit->length, max_n_matches, n_matches,
                            matches);
                }
                break;
            case TYPE_TOKEN:
                ret = _pattern_match(&patt->node->token, buf, offset,
                        max_n_matches, n_matches, matches);
                break;
        }

        patt->rep_count--;
    }
    if (ret.err == -1) {
        // clear out the entry in matches that we will be writing the address
        // of the end of the captured region, and eventually the start and end
        // offsets of the region, so that the NULL check will fail on the last
        // token
        if (captures && n_matches < max_n_matches) {
            *((char**) &matches[n_matches]) = buf;
        }

        // we decided not to keep repeating this pattern, so mark the end
        // offset and reserve an index in matches
        n_matches += captures;
    }
    if (ret.err == -1 && count >= patt->min) {
        // if matching more did not work, see if only matching up to
        // this point works
        patt->rep_count = 0;
        ret = _pattern_match(patt->next, buf, offset, max_n_matches,
                n_matches, matches);
        patt->rep_count = count;
    }

    if (ret.err != -1 && captures && count == 0) {
        if (n_matches < max_n_matches) {
            // if we found a match and this group captures, then ...
            char* end_loc = *((char**) &matches[n_matches]);
            // if this is the first pattern in the group,
            matches[n_matches].so = offset;
            // FIXME
            matches[n_matches].eo = offset + (unsigned) (end_loc - buf);
        }

        // we also need to increment the number of groups that have captured
        ret.n_captures++;
    }

    if (ret.err == -1 && count == 0 && patt->alt != NULL) {
        // if neither worked and we haven't yet used this pattern and we have
        // an alternative, try using that alternative
        // we need to check that the alternative exists because choosing no
        // options when there is no string left to match is not allowed

        // first put n_matches back and set this group to not capturing,
        // as we are not using it
        n_matches -= captures;
        ret = _pattern_match(patt->alt, buf, offset, max_n_matches,
                n_matches, matches);
    }

    return ret;
}

int pattern_match(token_t *patt, char *buf, size_t n_matches,
        match_t matches[]) {

    ret_info_t ret = _pattern_match(patt, buf, 0, n_matches, 0, matches);
    if (ret.err < 0) {
        return MATCH_FAIL;
    }
    if (ret.n_captures > n_matches) {
        return MATCH_OVERFLOW;
    }
    // set the first unused match_t to have start offset of -1
    if (ret.n_captures < n_matches) {
        matches[ret.n_captures].so = -1;
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

