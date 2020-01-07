#include <stdio.h>
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



void _pattern_free(hashmap *seen, pattern_t *patt) {
    token_t *token;
    if (patt_type(patt) == TYPE_TOKEN) {
        token = &patt->token;

        if (hash_insert(seen, token, NULL) == 0) {
            // only recurse if this is the first time seeing this node

            if (token->node != NULL) {
                // TODO node is never null for fully constructed FSMs
                patt_ref_dec(token->node);
                _pattern_free(seen, token->node);
            }

            if (token->alt != NULL) {
                patt_ref_dec((pattern_t*) token->alt);
                _pattern_free(seen, (pattern_t*) token->alt);
            }

            if (token->next != NULL) {
                patt_ref_dec((pattern_t*) token->next);
                _pattern_free(seen, (pattern_t*) token->next);
            }

            if (patt_ref_count(patt) == 0) {
                free(token);
            }
        }
    }
    else if (patt_ref_count(patt) == 0) {
        free(patt);
    }
}

void pattern_free(token_t *token) {
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);

    _pattern_free(&seen, (pattern_t*) token);
    hash_free(&seen);
}



static void _pattern_size(pattern_t *patt, size_info_t *counts,
        hashmap *seen) {

    if (hash_insert(seen, patt, NULL) != 0) {
        // already seen
        return;
    }

    if (patt_type(patt) == TYPE_TOKEN) {
        token_t *token = &patt->token;
        counts->n_tokens++;
        if (token->node != NULL) {
            _pattern_size(token->node, counts, seen);
        }
        if (token->next != NULL) {
            _pattern_size((pattern_t*) token->next, counts, seen);
        }
        if (token->alt != NULL) {
            _pattern_size((pattern_t*) token->alt, counts, seen);
        }
    }
    else {
        counts->n_patterns++;
    }

}


size_info_t pattern_size(token_t *patt) {
    size_info_t ret = {
        .n_patterns = 0,
        .n_tokens = 0
    };
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);

    _pattern_size((pattern_t*) patt, &ret, &seen);

    hash_free(&seen);
    return ret;
}



/*
 * removes a reference to the pattern by decreasing the reference count of the
 * pattern and freeing the pattern if its reference count is now 0
 */
static void _safe_free(pattern_t *patt) {
    patt_ref_dec(patt);
    if (patt_ref_count(patt) == 0) {
        free(patt);
    }
}



void _pattern_consolidate(token_t *patt, token_t *terminator, hashmap *seen) {

    if (patt == terminator) {
        // then we have looped back to the parent token which we are recursing
        // from, so we can stop
        return;
    }
    if (hash_insert(seen, patt, NULL) != 0) {
        // we have already consolidated this token, so we can stop
        return;
    }

    _pattern_consolidate(patt->next, terminator, seen);

    if (patt->alt != NULL) {
        _pattern_consolidate(patt->alt, terminator, seen);
    }

    if (patt_type(patt->node) == TYPE_TOKEN) {
        _pattern_consolidate(&patt->node->token, patt, seen);
    }

    // first, if this token points to a single-character literal or a character
    // class and is proceeded by a single-character literal or a character
    // class which has the same next pointer, then merge the two into one
    // character class
#define MERGEABLE_LIT(node) \
    (patt_type(node) == TYPE_LITERAL && node->lit.length == 1)

    token_t *alt = patt->alt;
    // we can merge with alt only if either both patt and alt are required
    // exactly once (min == max == 1) or both are optional
    if (alt != NULL && alt->next == patt->next &&
            (alt->max == 1 && patt->max == 1) && alt->min == patt->min) {

        if (MERGEABLE_LIT(alt->node)) {
            if (MERGEABLE_LIT(patt->node)) {
                char_class *cc = (char_class*) make_char_class();
                literal *patt_lit = &patt->node->lit;
                literal *alt_lit = &alt->node->lit;

                // allow the two characters from patt and alt
                cc_allow(cc, patt_lit->word[0]);
                cc_allow(cc, alt_lit->word[0]);

                // we are now done with both alt_lit and patt_lit
                _safe_free((pattern_t*) patt_lit);
                _safe_free((pattern_t*) alt_lit);

                // and now make token point to cc
                patt->node = (pattern_t*) cc;
                patt_ref_inc((pattern_t*) cc);

                // we are replacing both patt and alt with just patt, so
                // set the alt of patt to alt's alt
                patt->alt = alt->alt;

                // we don't need to set patt-next since we already verified
                // that patt->next == alt->next, however we still have one less
                // reference to next since we are freeing alt
                if (alt->next != NULL) {
                    patt_ref_dec((pattern_t*) alt->next);
                }

                // and now we can free alt
                free(alt);
            }
            else if (patt_type(patt->node) == TYPE_CC) {
                char_class *cc = &patt->node->cc;
                literal *alt_lit = &alt->node->lit;

                // allow the character from alt
                cc_allow(cc, alt_lit->word[0]);

                // we are now done with alt_lit
                _safe_free((pattern_t*) alt_lit);

                // need to set patt's alt to the alt's alt
                patt->alt = alt->alt;

                // we have one less pointer to next
                if (alt->next != NULL) {
                    patt_ref_dec((pattern_t*) alt->next);
                }

                // and now we can free alt
                free(alt);
            }
        }
        else if (patt_type(alt->node) == TYPE_CC) {
            if (MERGEABLE_LIT(patt->node)) {
                literal *patt_lit = &patt->node->lit;
                char_class *cc = &alt->node->cc;

                // allow the character from patt
                cc_allow(cc, patt_lit->word[0]);

                // we are done with patt_lit
                _safe_free((pattern_t*) patt_lit);

                // we need to move the char class to patt, and since we are
                // gaining one reference to cc here and losing one by removing
                // alt, we don't need to modify its reference count
                patt->node = (pattern_t*) cc;
                
                // need to set patt's alt to the alt's alt
                patt->alt = alt->alt;

                // we have one less pointer to next
                if (alt->next != NULL) {
                    patt_ref_dec((pattern_t*) alt->next);
                }

                // and now we can free alt
                free(alt);
            }
            else if (patt_type(patt->node) == TYPE_CC) {
                char_class *patt_cc = &patt->node->cc;
                char_class *alt_cc = &alt->node->cc;

                // merge the two char classes into patt_cc
                cc_allow_from(patt_cc, alt_cc);

                // and now we are done with alt_cc
                _safe_free((pattern_t*) alt_cc);

                // need to set patt's alt to the alt's alt
                patt->alt = alt->alt;

                // we have one less pointer to next
                if (alt->next != NULL) {
                    patt_ref_dec((pattern_t*) alt->next);
                }

                // and now we can free alt
                free(alt);
            }
        }
    }
}


void pattern_consolidate(token_t *patt) {
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);

    _pattern_consolidate(patt, NULL, &seen);

    hash_free(&seen);
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
