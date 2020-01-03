#include <stdlib.h>
#include <string.h>


#include "hashmap.h"
#include "match.h"



/*
 * mallocs a token and copies non-pointer fields of src into it
 */
static token_t* token_cpy(token_t *src) {
    token_t *dst;
    if (token_captures(src)) {
        dst = (token_t*) make_capturing_token();
        dst->match_idx = src->match_idx;
    }
    else {
        dst = (token_t*) make_token();
    }
    dst->flags = src->flags;
    dst->tmp = src->tmp;
    dst->min = src->min;
    dst->max = src->max;
    return dst;
}

token_t* _token_deep_copy(hashmap *copied, token_t *token) {
    token_t *ret;

    if ((ret = hash_get(copied, token)) == NULL) {
        // if it's not in the map, we haven't copied it yet, so make a copy
        // and recursively copy the tokens pointed to by the token
        ret = token_cpy(token);

        // and map token to the newly created copy of it before making any
        // recursive calls
        hash_insert(copied, token, ret);

        // deep copy the tokens connected to this token too

        if (token->alt != NULL) {
            ret->alt = _token_deep_copy(copied, token->alt);
            patt_ref_inc((pattern_t*) ret->alt);
        }

        if (token->next != NULL) {
            ret->next = _token_deep_copy(copied, token->next);
            patt_ref_inc((pattern_t*) ret->next);
        }

        if (patt_type(token->node) == TYPE_TOKEN) {
            // if this token encapsulates tokens, we need to copy those too
            ret->node = (pattern_t*) _token_deep_copy(copied,
                    &token->node->token);
        }
        else {
            // otherwise there is no need to copy and we can just take their
            // pointers
            ret->node = token->node;
        }
        patt_ref_inc(ret->node);
    }
    // now ret points to the (already or newly) copied token
    return ret;
}

token_t* pattern_deep_copy(token_t *token) {
    hashmap copied;
    hash_init(&copied, &ptr_hash, &ptr_cmp);

    token_t *ret = _token_deep_copy(&copied, token);

    hash_free(&copied);
    return ret;
}




/*
 * attempts to match as much of the given token as possible to the given
 * buffer, with offset being the index of the first character in buf in the
 * main string being matched, used only to calculate capturing group offsets
 *
 * returns the size of the region captured by the passed in token "patt", or
 * -1 on failure
 *
 * TODO pass in min + max (so loops in loops can work) and capturing (so don't
 * capture one group multiple times)
 */
static int _pattern_match(token_t *patt, char *buf, int offset,
        size_t n_matches, match_t matches[]) {

    int ret = -1;

    if (patt == NULL) {
        return (*buf == '\0') ? 0 : -1;
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
                            n_matches, matches);
                }
                break;
            case TYPE_LITERAL:
                lit = &patt->node->lit;
                if (strncmp(buf, lit->word, lit->length) == 0) {
                    ret = _pattern_match(patt, buf + lit->length,
                            offset + lit->length, n_matches, matches);
                }
                break;
            case TYPE_TOKEN:
                ret = _pattern_match(&patt->node->token, buf, offset,
                        n_matches, matches);
                break;
        }

        patt->rep_count--;
    }
    if (ret == -1) {
        // clear out the entry in matches that we will be writing the address
        // of the end of the captured region, and eventually the start and end
        // offsets of the region, so that the NULL check will fail on the last
        // token
        if (captures && patt->match_idx < n_matches) {
            *((char**) &matches[patt->match_idx]) = buf;
        }
    }
    if (ret == -1 && count >= patt->min) {
        // if matching more did not work, see if only matching up to
        // this point works
        patt->rep_count = 0;
        ret = _pattern_match(patt->next, buf, offset, n_matches, matches);
        patt->rep_count = count;
    }

    if (ret != -1 && captures && count == 0) {
        if (patt->match_idx < n_matches) {
            // if we found a match and this group captures, then ...
            char* end_loc = *((char**) &matches[patt->match_idx]);
            // if this is the first pattern in the group,
            matches[patt->match_idx].so = offset;
            matches[patt->match_idx].eo = offset + (unsigned) (end_loc - buf);
        }
    }

    if (ret == -1 && count == 0 && patt->alt != NULL) {
        // if neither worked and we haven't yet used this pattern and we have
        // an alternative, try using that alternative
        // we need to check that the alternative exists because choosing no
        // options when there is no string left to match is not allowed

        // first put n_matches back and set this group to not capturing,
        // as we are not using it
        ret = _pattern_match(patt->alt, buf, offset, n_matches, matches);
    }
    if (ret == -1 && captures && patt->match_idx < n_matches) {
        // if still no success, then this path was unsuccessful, so if we were
        // capturing, reset back to -1
        __builtin_memset(&matches[patt->match_idx], -1, 2 * sizeof(match_t));
    }

    return ret;
}

int pattern_match(token_t *patt, char *buf, size_t n_matches,
        match_t matches[]) {

    memset(matches, -1, n_matches * sizeof(match_t));
    int ret = _pattern_match(patt, buf, 0, n_matches, matches);
    if (ret < 0) {
        return MATCH_FAIL;
    }
    return 0;
}



static void _patt_free(pattern_t *patt) {
    token_t *token;
    if (patt_type(patt) == TYPE_TOKEN) {
        token = &patt->token;
        pattern_free(token);
    }
    else if (patt_ref_count(patt) == 0) {
        free(patt);
    }
}

void pattern_free(token_t *token) {
    if (patt_ref_count((pattern_t*) token) == 0) {

        // node is never null, so safe to free without checking
        patt_ref_dec(token->node);
        _patt_free(token->node);

        if (token->alt != NULL) {
            patt_ref_dec((pattern_t*) token->alt);
            _patt_free((pattern_t*) token->alt);
        }

        if (token->next != NULL) {
            patt_ref_dec((pattern_t*) token->next);
            _patt_free((pattern_t*) token->next);
        }

        free(token);
    }
}


#define SEEN 1

void mark_seen(token_t *token) {
    token->tmp |= SEEN;
}

int is_seen(token_t *token) {
    return (token->tmp & SEEN) != 0;
}

void unmark_seen(token_t *token) {
    token->tmp &= ~SEEN;
}


// TODO add double checking trap?
int pattern_connect(token_t *patt, token_t *to) {
    int ret = -1;

    if (is_seen(patt)) {
        return ret;
    }

    mark_seen(patt);

    if (patt->next == NULL) {
        // we can link patt to "to"
        patt->next = to;
        patt_ref_inc((pattern_t*) to);
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

    unmark_seen(patt);
    return ret;
}

int pattern_or(token_t *patt, token_t *opt) {
    for (; patt->alt != NULL; patt = patt->alt);
    patt->alt = opt;
    patt_ref_inc((pattern_t*) opt);
    return 0;
}

