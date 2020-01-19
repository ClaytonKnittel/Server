#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>


#include "../hashmap.h"
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
    // FIXME make this thread safe!
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
        __builtin_memset(&matches[patt->match_idx], -1, sizeof(match_t));
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



void _pattern_free(token_t *token) {

    pattern_t *node = token->node,
              *alt = (pattern_t*) token->alt,
              *next = (pattern_t*) token->next;

    // set all connections to NULL so they won't be visited multiple times
    // through this token
    token->node = NULL;
    token->alt = NULL;
    token->next = NULL;

    if (node != NULL) {
        if (patt_type(node) == TYPE_TOKEN) {
            _pattern_free(&node->token);
        }
        patt_ref_dec(node);
        if (patt_ref_count(node) == 0) {
            free(node);
        }
    }

    if (alt != NULL) {
        _pattern_free(&alt->token);
        patt_ref_dec(alt);
        if (patt_ref_count(alt) == 0) {
            free(alt);
        }
    }

    if (next != NULL) {
        _pattern_free(&next->token);
        patt_ref_dec(next);
        if (patt_ref_count(next) == 0) {
            free(next);
        }
    }
}

void pattern_free(token_t *token) {
    // increase ref_count of this token by 1 so no token in _pattern_free frees
    // it, and we can safely always free it here
    patt_ref_inc((pattern_t*) token);

    _pattern_free(token);
    free(token);
}



typedef struct idx_off {
    // index of this token (where it will appear in the file, also the value
    // to replace pointers to this with in file)
    int idx;
    // byte offset of the beginning of this pattern_t in the file
    int offset;
} idx_off_t;

// mirror of token_t struct, but stripped down be as small as possible and with
// pointers replaced by ints (for indices)
struct tiny_token {
    int flags;

    // no need for tmp

    // indices of these 3 corresponding pointers
    int node, alt, next;

    struct {
        int min, max;
    };
};

// only need match_idx for capturing tokens
struct tiny_capturing_token {
    struct tiny_token base;
    unsigned match_idx;
};


static int tiny_patt_size(pattern_t* patt) {
    switch (patt_type(patt)) {
        case TYPE_CC:
            return sizeof(char_class);
        case TYPE_LITERAL:
            return sizeof(literal) + patt->lit.length;
        case TYPE_TOKEN:
            return token_captures(&patt->token) ?
                sizeof(struct tiny_capturing_token) :
                sizeof(struct tiny_token);
        default:
            return 0;
    }
}

/*
 * helper for pattern_store, passes through all tokens/patterns in the pattern
 * and assigns them indices and offset in the file to be stored by associating
 * a dynamically allocated idx_off_t with each toekn/pattern
 *
 * token_count points to a static counter used to assign indices and offsets to
 * patterns (these indices align with the locations the file that the tokens
 * will be stored, and the offsets tell where in the file it should be stored)
 *
 * returns >= 0 on success (equal to the total number of bytes taken by whole
 * pattern) or -1 on failure
 */
static int _pattern_map_create(pattern_t *patt, hashmap *idces,
        idx_off_t* token_count) {

    idx_off_t* patt_idx;
    int patt_size;
    int ret = 0;

    // initialize patt_idx pointer for this pattern and test if we have already
    // visited this pattern (in which case we should skip it and return)
    patt_idx = (idx_off_t*) malloc(sizeof(idx_off_t));
    if (patt_idx == NULL) {
        fprintf(stderr, "Unable to malloc idx_off_t, reason: %s\n",
                strerror(errno));
        return -1;
    }
    if (hash_insert(idces, patt, patt_idx) != 0) {
        free(patt_idx);
        return 0;
    }

    patt_size = tiny_patt_size(patt);

    patt_idx->idx = token_count->idx;
    patt_idx->offset = token_count->offset;
    token_count->idx++;
    token_count->offset += patt_size;

    if (patt_type(patt) == TYPE_TOKEN) {
        token_t *token = &patt->token;
        ret = _pattern_map_create(token->node, idces, token_count);
        patt_size += ret;
        if (ret >= 0 && token->next != NULL) {
            ret = _pattern_map_create((pattern_t*) token->next, idces,
                    token_count);
            patt_size += ret;
        }
        if (ret >= 0 && token->alt != NULL) {
            ret = _pattern_map_create((pattern_t*) token->alt, idces,
                    token_count);
            patt_size += ret;
        }
    }

    return (ret < 0) ? ret : patt_size;
}


/*
 * packs the pattern into the buffer by iterating through the entries in idces
 * and writing to the proper locations in the buffer
 *
 * also frees all dynamically allocated idx_off_t data associated with each
 * pattern in the hashmap
 */
static void _pattern_pack(hashmap *idces, void* buffer) {
    pattern_t* patt;
    idx_off_t* patt_idx;
    void* loc;

    hashmap_for_each(idces, patt, patt_idx) {
        printf("packing %p (%d, idx %d)\n", patt, patt_idx->offset, patt_idx->idx);
        loc = ((char*) buffer) + patt_idx->offset;
        switch (patt_type(patt)) {
            case TYPE_CC:
                __builtin_memcpy(loc, patt, sizeof(char_class));
                break;
            case TYPE_LITERAL:
                memcpy(loc, patt, sizeof(literal) + patt->lit.length);
                break;
            case TYPE_TOKEN:
                __builtin_memcpy(loc, patt, token_captures(&patt->token) ?
                        sizeof(struct tiny_capturing_token) :
                        sizeof(struct tiny_token));
                break;
        }
        free(patt_idx);
    }
}

/*
 * stored in following format (binary)
 *
 * total size of pattern in bytes (8 bytes)
 * pattern data (all tokens/patterns follow sequentially, each tightly packed
 *     referencing each other by index in this list (i.e. 0 would indicate a
 *     pointer to the first token in the file))
 *
 */
int pattern_store(const char* path, token_t *patt) {
    hashmap idces;
    int patt_size;
    int fd = open(path, O_CREAT, S_IRUSR | S_IWUSR);
    // will point to temporary buffer which will store entire file contents,
    // so we only need to make one large write in the end
    void* buffer;

    if (fd == -1) {
        fprintf(stderr, "Could not create file %s, reason: %s", path,
                strerror(errno));
        return fd;
    }

    idx_off_t* token_count = (idx_off_t*) malloc(sizeof(idx_off_t));
    if (token_count == NULL) {
        fprintf(stderr, "Unable to malloc idx_off_t, reason: %s\n",
                strerror(errno));
        close(fd);
        return -1;
    }

    hash_init(&idces, &ptr_hash, &ptr_cmp);

    patt_size = _pattern_map_create((pattern_t*) patt, &idces, token_count);

    if (patt_size > 0) {
        // if recursive call succeeded, then we can save to file
        buffer = malloc(patt_size);
        if (buffer == NULL) {
            fprintf(stderr, "Unable to malloc %d bytes for pattern buffer, "
                    "reason: %s\n", patt_size, strerror(errno));
        }
        else {
            _pattern_pack(&idces, buffer);
            write(fd, buffer, patt_size);
        }
    }

    hash_free(&idces);
    free(token_count);
    close(fd);
    return patt_size < 0 ? patt_size : 0;;
}


token_t* pattern_load(const char* path) {
    token_t *ret = NULL;


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


    // try elevation before recursing any, which we are only able to do if:
    //  - neither patt nor patt->node captures, and either
    //      - patt is once-required (min = max = 1)
    //      - patt->node only connects back to patt (next = patt, alt = NULL)
    //          and  is 1*n or 0*n, for some n (even infinity)
    //  - only patt captures and patt->node is the only token encapsulated by
    //      patt and patt->node is once-required
    //
    // if patt->node captures, we cannot gracefully elevate, as capturing
    // tokens are slightly larger than regular ones, so it would need slightly
    // more space than patt has to offer if it were to take its spot
    if (patt_type(patt->node) == TYPE_TOKEN &&
            !token_captures(&patt->node->token)) {

        token_t *node = &patt->node->token;
        int node_only = (node->next == patt && node->alt == NULL);

        if ((!token_captures(patt) && (
                    (patt->min == 1 && patt->max == 1) ||
                     (node_only && node->min <= 1)))
                || (node_only && node->min == 1 && node->max == 1)) {

            // if this is a once-required group, then just elevate the token
            // it contains

            // first disconnect the circular references back to patt->node from
            // that subgraph, as we will be reconnecting it to patt's next and
            // alt. We know all references to patt->node have to be in the
            // subgraph contained by patt->node, as pattern's nodes are all
            // private
            pattern_disconnect(node, patt);
            if (patt_type((node)->node) == TYPE_TOKEN) {
                pattern_reconnect((token_t*) node->node,
                        (token_t*) patt->node, patt);
            }

            // save patt's connections
            token_t *next = patt->next;
            token_t *alt = patt->alt;

            // take whole flags because we need the capturing flag of patt
            // to be preserved
            int flags = patt->flags;
            int match_idx = patt->match_idx;

            // in every case, this is what we want to reassign min & max of the
            // token to, since one of patt and node will be 0*1 or 1*1
            int min = patt->min * node->min;
            int max = (patt->max == -1) ? patt->max :
                (node->max == -1) ? node->max : patt->max * node->max;

            // now copy over all of patt->node's data
            *patt = *node;
            patt->flags = flags;
            patt->match_idx = match_idx;
            patt->min = min;
            patt->max = max;

            // and finally hook up the subgraph to patt's next and alt
            if (next != NULL) {
                // because we are removing patt
                patt_ref_dec((pattern_t*) next);
                pattern_connect(patt, next);
            }
            if (alt != NULL) {
                // because we are removing patt
                patt_ref_dec((pattern_t*) alt);
                pattern_or(patt, alt);
            }


            // and lastly free the old patt->node
            free(node);
        }
        else if (patt->min == 0 && patt->max == 1) {

        }

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
            (alt->max == 1 && patt->max == 1) && alt->min == patt->min &&
            !token_captures(patt) && !token_captures(alt)) {

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

    token_t *next = patt->next;
    // if this is a string proceeded by another string with no alt, and the
    // number of times we are allowed to repeat both is fixed, and we are the
    // only token referencing next, then we can merge the two into a single
    // literal
    // TODO add check to make sure don't do this with big words/many repeats
    if (next != NULL && next->alt == NULL &&
            patt_type(patt->node) == TYPE_LITERAL &&
            patt_type(next->node) == TYPE_LITERAL &&
            patt->min == patt->max && next->min == next->max &&
            patt_ref_count((pattern_t*) next) == 1 &&
            !token_captures(patt) && !token_captures(next)) {
        
        int n = patt->max; // number of times to repeat patt
        int m = next->max; // number of times to repeat next

        literal *tlit = &patt->node->lit;
        literal *nlit = &next->node->lit;

        int tlen = tlit->length; // length of patt's word
        int nlen = nlit->length; // length of next's word

        literal *comb = (literal*) make_literal(n * tlen + m * nlen);
        for (int i = 0; i < n; i++) {
            memcpy(&comb->word[i * tlen], &tlit->word[0], tlen);
        }
        for (int i = 0; i < m; i++) {
            memcpy(&comb->word[n * tlen + i * nlen], &nlit->word[0], nlen);
        }

        comb->length = n * tlen + m * nlen;

        patt->node = (pattern_t*) comb;
        patt_ref_inc((pattern_t*) comb);

        _safe_free((pattern_t*) tlit);
        _safe_free((pattern_t*) nlit);

        // take next's spot, we know that next's alt is NULL, so we just need
        // to set out next to next's next
        patt->next = next->next;

        // we can now safely free next, as we know that it's ref count was 1
        free(next);

        // now that we have combined them, we only need to consume this once
        patt->min = 1;
        patt->max = 1;
    }
    else if (patt_type(patt->node) == TYPE_LITERAL &&
            patt->min == patt->max && patt->max > 1) {

        // then we can make this one long literal
        int n = patt->max;
        literal *lit = &patt->node->lit;
        int len = lit->length;

        literal *comb = (literal*) make_literal(n * len);
        for (int i = 0; i < n; i++) {
            memcpy(&comb->word[i * len], &lit->word[0], len);
        }

        comb->length = n * len;

        patt->node = (pattern_t*) comb;
        patt_ref_inc((pattern_t*) comb);

        _safe_free((pattern_t*) lit);

        // now that we have combined them, we only need to consume this once
        patt->min = 1;
        patt->max = 1;
    }





}


void pattern_consolidate(token_t *patt) {
    // acts as a hashset of tokens that have already been consolidated
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
    else if (patt->next == to) {
        // don't recurse into to
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


int pattern_reconnect(token_t *patt, token_t *from, token_t *to) {
    int ret = -1;

    if (is_seen(patt)) {
        return ret;
    }

    mark_seen(patt);

    if (patt->next == from) {
        // we can link patt to "to"
        patt->next = to;
        patt_ref_inc((pattern_t*) to);
        patt_ref_dec((pattern_t*) from);
        ret = 0;
    }
    else if (patt->next == to) {
        // don't recurse into to
    }
    else if (pattern_reconnect(patt->next, from, to) != -1) {
        ret = 0;
    }
    if (patt->alt == from) {
        patt->alt = to;
        patt_ref_inc((pattern_t*) to);
        patt_ref_dec((pattern_t*) from);
        ret = 0;
    }
    else if (patt->alt != NULL) {
        if (pattern_reconnect(patt->alt, from, to) != -1) {
            ret = 0;
        }
    }

    unmark_seen(patt);
    return ret;
}

int pattern_disconnect(token_t *patt, token_t *from) {
    int ret = -1;

    if (is_seen(patt)) {
        return ret;
    }

    mark_seen(patt);

    if (patt->next == from) {
        // we can unlink patt
        patt->next = NULL;
        patt_ref_dec((pattern_t*) from);
        ret = 0;
    }
    else if (patt->next == NULL) {
        // don't recurse into what has been unset
    }
    else if (pattern_disconnect(patt->next, from) != -1) {
        ret = 0;
    }
    if (patt->alt == from) {
        patt->alt = NULL;
        patt_ref_dec((pattern_t*) from);
        ret = 0;
    }
    else if (patt->alt != NULL) {
        if (pattern_disconnect(patt->alt, from) != -1) {
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

