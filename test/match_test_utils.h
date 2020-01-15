#include <stdlib.h>
#include <string.h>

#include "../src/augbnf.h"
#include "../src/hashmap.h"
#include "../src/vprint.h"
#include "../src/match.h"


typedef struct size_info {
    size_t n_patterns;
    size_t n_tokens;
} size_info_t;



static void _pattern_counts(pattern_t *patt, size_info_t *counts,
        hashmap *seen) {

    if (hash_insert(seen, patt, NULL) != 0) {
        // already seen
        return;
    }

    if (patt_type(patt) == TYPE_TOKEN) {
        token_t *token = &patt->token;
        counts->n_tokens++;
        if (token->node != NULL) {
            _pattern_counts(token->node, counts, seen);
        }
        if (token->next != NULL) {
            _pattern_counts((pattern_t*) token->next, counts, seen);
        }
        if (token->alt != NULL) {
            _pattern_counts((pattern_t*) token->alt, counts, seen);
        }
    }
    else {
        counts->n_patterns++;
    }

}


/*
 * calculates the number of tokens and patterns in this pattern structure,
 * returning the result as a (n_patterns, n_tokens) pair
 */
size_info_t pattern_counts(token_t *patt) {
    size_info_t ret = {
        .n_patterns = 0,
        .n_tokens = 0
    };
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);

    _pattern_counts((pattern_t*) patt, &ret, &seen);

    hash_free(&seen);
    return ret;
}




static void _pattern_size(pattern_t *patt, size_t *size, hashmap *seen) {

    if (hash_insert(seen, patt, NULL) != 0) {
        // already seen
        return;
    }

    if (patt_type(patt) == TYPE_TOKEN) {
        token_t *token = &patt->token;
        if (token->node != NULL) {
            _pattern_size(token->node, size, seen);
        }
        if (token->next != NULL) {
            _pattern_size((pattern_t*) token->next, size, seen);
        }
        if (token->alt != NULL) {
            _pattern_size((pattern_t*) token->alt, size, seen);
        }
    }
    switch (patt_type(patt)) {
        case TYPE_LITERAL:
            (*size) += sizeof(literal) + patt->lit.length;
            break;
        case TYPE_CC:
            (*size) += sizeof(char_class);
            break;
        case TYPE_TOKEN:
            (*size) += sizeof(token_t);
    }

}


/*
 * calculates the size of allocated memory taken by this pattern in bytes
 */
size_t pattern_size(token_t *patt) {
    size_t size = 0;
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);

    _pattern_size((pattern_t*) patt, &size, &seen);

    hash_free(&seen);
    return size;
}


static unsigned count_;

static void _bnf_print(token_t *patt, hashmap *seen) {
    char *buf;
    char_class *cc;
    literal *lit;

    unsigned *c = (unsigned*) malloc(sizeof(unsigned));
    if (hash_insert(seen, patt, c) != 0) {
        // already been seen
        free(c);
        return;
    }
    *c = count_++;

    if (patt_type(patt->node) == TYPE_TOKEN) {
        _bnf_print(&patt->node->token, seen);
    }
    if (patt->alt != NULL) {
        _bnf_print(patt->alt, seen);
    }
    if (patt->next != NULL) {
        _bnf_print(patt->next, seen);
    }

    /*if (!token_captures(patt)) {
        return;
    }*/
    printf("(0x%p) p%u: %d*%d (tmp = %d) (r = %d)", patt,
            *(unsigned*) hash_get(seen, patt), patt->min, patt->max, patt->tmp,
            patt_ref_count((pattern_t*) patt));
    if (token_captures(patt)) {
        printf("(mid: %u) ", patt->match_idx);
    }
    printf("\t");
    printf("(0x%p) (r = %d)  ", patt->node, patt_ref_count(patt->node));
    switch (patt_type(patt->node)) {
        case TYPE_CC:
            cc = &patt->node->cc;
            printf("<");
            for (unsigned char c = 0; c < NUM_CHARS; c++) {
                if (cc_is_match(cc, c)) {
                    printf("%c", c);
                }
            }
            printf(">");
            break;
        case TYPE_LITERAL:
            lit = &patt->node->lit;
            buf = (char*) malloc(lit->length + 1);
            memcpy(buf, lit->word, lit->length);
            buf[lit->length] = '\0';
            printf("\"%s\" (len %u)", buf, lit->length);
            free(buf);
            break;
        case TYPE_UNRESOLVED:
            lit = &patt->node->lit;
            buf = (char*) malloc(lit->length + 1);
            memcpy(buf, lit->word, lit->length);
            buf[lit->length] = '\0';
            printf("%s", buf);
            free(buf);
            break;
    }
    if (patt_type(patt->node) == TYPE_TOKEN) {
        printf("for p%u\t", *(unsigned*) hash_get(seen, &patt->node->token));
    }
    else {
        printf(" (r %d)", patt_ref_count(patt->node));
    }
    if (patt->alt != NULL) {
        printf(" or p%u:\t", *(unsigned*) hash_get(seen, patt->alt));
    }
    if (patt->next != NULL) {
        printf("  to p%u:\t", *(unsigned*) hash_get(seen, patt->next));
    }
    printf("\n");
}

static void bnf_print(token_t *patt) {
    count_ = 0;
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);
    _bnf_print(patt, &seen);

    void *k;
    unsigned *count;
    hashmap_for_each(&seen, k, count) {
        free(count);
    }
    hash_free(&seen);
}




#define CON_ERR(msg, ...) \
    vprintf(P_RED "BNF Consistency Checker failed " P_RESET msg, ## __VA_ARGS__)


// marked rigth before a token recurses to its children (to check for cycles),
// and unmarked after processing of this token and all of its descendents has
// completed
#define PROCESSING 1

// set after processing of a sub-token, all elements within the sub-token are
// to be marked as NON_REFERENCEABLE, which means that if we later find that
// a token references this token, there exists a reference to a token that is
// meant to be entirely self-contained
#define NON_REFERENCEABLE 2


static void mark_processing(token_t *t) {
    t->tmp |= PROCESSING;
}
static void unmark_processing(token_t *t) {
    t->tmp &= ~PROCESSING;
}

static int is_processing(token_t *t) {
    return (t->tmp & PROCESSING) != 0;
}

static void mark_non_referenceable(token_t *t) {
    t->tmp |= NON_REFERENCEABLE;
}

static int is_non_referenceable(token_t *t) {
    return (t->tmp & NON_REFERENCEABLE) != 0;
}


static int _bnf_consistency_check(token_t *patt, const token_t *terminator,
        hashmap *counters) {
    int ret = 0;

    if (is_non_referenceable(patt)) {
        CON_ERR("Reference to token from subtoken which was already processed "
                "found, at %p\n", patt);
        return -1;
    }
    if (is_processing(patt)) {
        // cycle detected
        CON_ERR("Cycle found in FSM on node %p\n", patt);
        return -1;
    }
    if (is_non_referenceable(patt)) {
        CON_ERR("Reference to token in submodule found, either the subtoken "
                "is not entirely self-contained, or some external token "
                "is illegally referencing it\n");
        return -1;
    }

    if (patt->next == NULL && terminator != NULL) {
        // we have reached a terminating state within a sub-pattern of a token
        CON_ERR("Reached a terminating state within a subtoken\n");
        return -1;
    }

    unsigned *ref_count = (unsigned*) calloc(1, sizeof(unsigned));
    if (hash_insert(counters, patt, ref_count) != 0) {
        // we have already visited this token
        free(ref_count);
        return 0;
    }

    mark_processing(patt);

    if (patt->node == NULL) {
        CON_ERR("Token %p has NULL node\n", patt);
        return -1;
    }

    if (patt_type(patt->node) == TYPE_TOKEN) {
        // need to fully recurse on submodule
        hashmap subcounters;
        hash_init(&subcounters, &ptr_hash, &ptr_cmp);

        // need to insert our own counter into the subcounter map since the
        // tokens in the submodule will refer back to us
        hash_insert(&subcounters, patt, ref_count);

        ret = _bnf_consistency_check(&patt->node->token, patt, &subcounters);
        
        pattern_t *subtoken;
        unsigned *counter;
        // and now add back each of the counters to 
        hashmap_for_each(&subcounters, subtoken, counter) {
            if (subtoken == (pattern_t*) patt) {
                // skip ourself
                continue;
            }
            if (hash_insert(counters, subtoken, counter) != 0) {
                if (patt_type(subtoken) != TYPE_TOKEN) {
                    // then we need to combine the two reference counts
                    unsigned *orig_count = (unsigned*) hash_get(counters, subtoken);
                    (*orig_count) += *counter;
                }
                else if (ret == 0) {
                    // found a node we have already visited, this means there
                    // was a second reference to this submodule, which means
                    // it is not entirely self-contained
                    CON_ERR("Reference to token %p in submodule of %p already "
                            "exists\n", patt, subtoken);
                    ret = -1;
                }
                free(counter);
            }
            // we are not allowed to ever refer to this token again since it's
            // in a submodule
            if (patt_type(subtoken) == TYPE_TOKEN) {
                mark_non_referenceable(&subtoken->token);
            }
        }
        hash_free(&subcounters);

        unsigned *node_ref_count = (unsigned*) hash_get(counters, patt->node);
        (*node_ref_count)++;
    }
    else {
        // this is a token pointing to a basic pattern, either a literal or
        // char class. Regardless, we just need to add it to the ref count
        // map for ref count checking
        unsigned *basic_ref_count;
        if ((basic_ref_count = hash_get(counters, patt->node)) == NULL) {
            basic_ref_count = (unsigned*) calloc(1, sizeof(unsigned));
            if (hash_insert(counters, patt->node, basic_ref_count)) {
                // what?
                free(basic_ref_count);
                CON_ERR("Weird hashmap err, could not insert element which "
                        "did not exist\n");
                ret = -1;
            }
        }
        (*basic_ref_count)++;
    }

    if (ret == 0) {
        if (patt->next != terminator) {
            // only need to recursively check next if it is not the terminator
            ret = _bnf_consistency_check(patt->next, terminator, counters);
            if (ret == 0) {
                unsigned *next_ref_count = (unsigned*) hash_get(counters, patt->next);
                (*next_ref_count)++;
            }
        }
        else if (patt->next != NULL) {
            unsigned *next_ref_count = (unsigned*) hash_get(counters, patt->next);
            (*next_ref_count)++;
        }
    }
    if (ret == 0 && patt->alt != NULL) {
        ret = _bnf_consistency_check(patt->alt, terminator, counters);
        // if and only if this is referenced by any other token outside of
        // "patt" and, potentially, the tokens within the submodule its node
        // points to (if its node is itself a token) then, assuming the
        // reference counts are correct, the reference count of this pattern
        // would not be equal here. If any other token which is reachable
        // via patt->alt references back to patt->alt, then the above
        // recursive call would detect a cycle. So, then, the only way for a
        // token to illegally refer to patt->alt would be if the current
        // reference count is not the total reference count. If it is detected
        // that they are equal here, but in reality there is another token
        // referring to this one here, then the reference count check performed
        // at the end will detect an error
        if (ret == 0) {
            unsigned *alt_ref_count = (unsigned*) hash_get(counters, patt->alt);
            (*alt_ref_count)++; // for this token
            if (patt_ref_count((pattern_t*) patt->alt) != *alt_ref_count) {
                CON_ERR("Illegal reference to a token %p which is an alt of some "
                        "token (found count %u, expect %u)\n", patt->alt,
                        patt_ref_count((pattern_t*) patt->alt), *alt_ref_count);
                ret = -1;
            }
        }
    }

    unmark_processing(patt);

    return ret;
}

/*
 * performs a rigorous consistency check on the pattern, according to the
 * standards explained at the top of src/match.h
 */
static int bnf_consistency_check(token_t *patt) {
    hashmap counters;
    hash_init(&counters, &ptr_hash, &ptr_cmp);

    int ret = _bnf_consistency_check(patt, NULL, &counters);

    // reset all tmp fields and free all auxiliary data before returning
    pattern_t *p;
    unsigned *ref_count;
    hashmap_for_each(&counters, p, ref_count) {
        if (patt_type(p) == TYPE_TOKEN) {
            p->token.tmp = 0;
        }

        if (ret == 0 && patt_ref_count(p) != *ref_count) {
            // ref count incorrect in pattern
            CON_ERR("ref count incorrect for token %p, expect %u, but found "
                    "%u\n", p, *ref_count, patt_ref_count(p));
            ret = -1;
        }
        free(ref_count);
    }

    hash_free(&counters);
    return ret;
}



static int _tmp_check(hashmap *seen, token_t *token) {
    if (hash_insert(seen, token, NULL) == 0) {
        // if it wasn't seen before, check its tmp field and recurse on its
        // children
        if (token->tmp != 0) {
            return -1;
        }
        if (patt_type(token->node) == TYPE_TOKEN) {
            if (_tmp_check(seen, &token->node->token) != 0) {
                return -1;
            }
        }
        if (token->next != NULL) {
            if (_tmp_check(seen, token->next) != 0) {
                return -1;
            }
        }
        if (token->alt != NULL) {
            if (_tmp_check(seen, token->alt) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/*
 * checks to make sure this token and all of its descendents' tmp fields are
 * zero
 */
static int tmp_check(token_t *token) {
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);

    int ret = _tmp_check(&seen, token);

    hash_free(&seen);
    return ret;
}

