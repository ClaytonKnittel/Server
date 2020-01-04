#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "augbnf.h"
#include "hashmap.h"
#include "vprint.h"
#include "util.h"


// possible sources of data
#define PARSING_FILE 1
#define PARSING_BUF 2


// we add an additional type of node which fits in the patter_t struct which
// contains only a name and is to later be resolved when compiling the bnf
#define TYPE_UNRESOLVED 0x3

// because an unresolved node is just the name of the token it points to, we
// can use the literal structure, as it is of the same form
typedef literal unresolved;


#define BNF_ERR(msg, ...) \
    vprintf(P_RED "BNF compiler error" P_YELLOW " (line %lu)" P_RED ": " \
            P_RESET msg, state->linen, ## __VA_ARGS__)

#define BNF_ERR_NOLINE(msg, ...) \
    vprintf(P_RED "BNF compiler error: " \
            P_RESET msg, ## __VA_ARGS__)

#define BNF_WARNING(msg, ...) \
    vprintf(P_YELLOW "BNF compiler warning: " P_RESET msg, ## __VA_ARGS__)



// static globals which hold character classes used in parsing
static struct parsers {
    char_class whitespace;
    char_class num;
    // number or '*'
    char_class quantifier;
    char_class unreserved;
    char_class quote;
    char_class all;
} parsers;

// initializes the parsers, to be called once at the beginning of a bnf_parse
// call
static __inline void init_parsers() {
    __builtin_memset(&parsers, 0, sizeof(struct parsers));

    cc_allow_whitespace(&parsers.whitespace);
    cc_allow_num(&parsers.num);

    cc_allow_num(&parsers.quantifier);
    cc_allow(&parsers.quantifier, '*');

    cc_allow_alphanum(&parsers.unreserved);
    cc_allow(&parsers.unreserved, '-');
    cc_allow(&parsers.unreserved, '_');
    cc_allow(&parsers.unreserved, '.');
    cc_allow(&parsers.unreserved, '!');
    cc_allow(&parsers.unreserved, '~');
    cc_allow(&parsers.unreserved, '@');

    cc_allow(&parsers.quote, '"');
    cc_allow_all(&parsers.all);
}


/*
 * skips over the whitespace pointed to by buf until a non-whitespace character
 * is reached, upon which it returns a pointer to that location
 */
static char* skip_whitespace(char* buf) {
    while (cc_is_match(&parsers.whitespace, *buf)) {
        buf++;
    }
    return buf;
}

/*
 * same as skip_whitespace, but for alphanumeric characters (upper and
 * lowercase letters and digits)
 */
static char* skip_unreserved(char* buf) {
    while (cc_is_match(&parsers.unreserved, *buf)) {
        buf++;
    }
    return buf;
}



/*
 * one is created for each call to bnf_parse, this contains all information
 * necessary to compile a bnf string
 */
typedef struct parse_state {
    // a hashmap of all the symbol names to their definitions
    hashmap rules;
    // which line number we are on while parsing
    size_t linen;
    token_t *main_rule;

    // the pointer share between recursive calls of where in memory we are
    // currently parsing the bfn
    char *buf;

    // we can either be reading from an open file or from a buffer in memory
    // either PARSING_FILE or PARSING_BUF
    int read_from;

    // the number of capturing groups we have found, used to set match_idx for
    // each of the capturing groups
    unsigned n_captures;
    union {
        struct {
            FILE *file;
            // pointer to the beginning of the memory region read into from
            // the file, which must not be modified outside of getline for
            // proper memory cleanup
            char *file_buf;
            // size of file_buf, used in repeat calls to getline
            size_t line_size;
        };
        struct {
            // pointer to user-given memory
            const char* buffer;
            // pointer to malloced memory, which is copied from buffer
            char* segment;
            // tracks where in buffer we have read up to
            size_t buf_loc;
            // size of buffer in bytes
            const size_t buf_size;
        };
    };
} parse_state;


static int read_line(parse_state *state) {
    int ret;

    if (state->read_from == PARSING_FILE) {
        ret = getline(&state->file_buf, &state->line_size, state->file);
        if (ret == -1) {
            // EOF
            return eof;
        }
        state->buf = state->file_buf;
    }
    else {
        size_t search_len = state->buf_size - state->buf_loc;
        if (search_len == 0) {
            return eof;
        }
        const char* cur_buf = state->buffer + state->buf_loc;
        const char* next_buf = memchr(cur_buf, '\n', search_len);
        if (next_buf == NULL) {
            // we are now at the end of the buffer
            next_buf = cur_buf + search_len;
        }
        size_t size = (size_t) (next_buf - cur_buf);
        state->segment = (char*) realloc(state->segment, size + 1);
        memcpy(state->segment, cur_buf, size);
        state->segment[size] = '\0';

        // add 1 to skip the newline that was found
        state->buf_loc = (size_t) (next_buf - state->buffer) + 1;
        // if we exceeded the size of the buffer, set it to the buffer size
        state->buf_loc = (state->buf_loc > state->buf_size) ?
            state->buf_size : state->buf_loc;
        state->buf = state->segment;
    }
    state->linen++;
    return 0;
}


static char* get_next_unmatching(char_class*, char*);

// gets next non-whitespace character, reading more lines from the file if
// necessary
static int get_next_non_whitespace(parse_state *state) {
    int ret;

    state->buf = get_next_unmatching(&parsers.whitespace, state->buf);
    while (*(state->buf) == '\0') {
        ret = read_line(state);
        if (ret != 0) {
            return ret;
        }
        state->buf = get_next_unmatching(&parsers.whitespace, state->buf);
    }
    return 0;
}



/*
 * scans buf until finding a character in the given char_class that is
 * not preceeded by an unescaped backslash
 */
static char* get_next_unescaped(char_class *cc, char* buf) {
    while (!cc_is_match(cc, *buf) && *buf != '\0') {
        if (*buf == '\\' && *(buf + 1) != '\0') {
            buf += 2;
        }
        else {
            buf++;
        }
    }
    return buf;
}


/*
 * scans buf until finding a character not in the given char_class
 */
static char* get_next_unmatching(char_class *cc, char* buf) {
    while (cc_is_match(cc, *buf)) {
        buf++;
    }
    return buf;
}

/*
 * parse all referenced tokens and literals out of the rule, stopping only
 * when the term_on character has been reached
 *
 * on success, this method returns a dynamically allocated token which contains
 * this entire rule, on failure, NULL is returned and errno is set
 */
static token_t* token_group_parse(parse_state *state, char term_on) {

    char *buf = state->buf;
    int retval;

    // to be set when it is known whether they are using AND or OR grouping
#define GROUP_AND   0x1
#define GROUP_OR    0x2
    int rule_grouping = 0;

    // the token to be returned, i.e. the token constructed in the first pass
    // through the loop
    token_t *ret = NULL;
    // the token made on the last pass of the loop (for linking)
    token_t *last = NULL;
    // the current token being created
    token_t *token;

#define RETURN_ERR(err_lvl) \
    errno = (err_lvl); \
    pattern_free(token); \
    if (ret != NULL) { \
        pattern_free(ret); \
    } \
    return NULL

#define PROPAGATE_ERR \
    pattern_free(token); \
    if (ret != NULL) { \
        pattern_free(ret); \
    } \
    return NULL

    do {
        retval = get_next_non_whitespace(state);
        if (retval != 0) {
            if (term_on != '\0') {
                BNF_ERR("unexpected EOF\n");
                errno = unexpected_eof;
                if (ret != NULL) {
                    pattern_free(ret);
                }
                return NULL;
            }
            // reached EOF
            return ret;
        }

        // since the preceeding method modified only buf in state
        buf = state->buf;

        token = (token_t*) make_token();
        if (token == NULL) {
            errno = memory_error;
            if (ret != NULL) {
                pattern_free(ret);
            }
            return token;
        }

        // search for quantifiers
        if (cc_is_match(&parsers.quantifier, *buf)) {
            // expect something of the form *, *<n>, <m>* or <m>*<n>
            if (*buf == '*') {
                token->min = 0;
            }
            else {
                char* m = buf;
                buf = get_next_unmatching(&parsers.num, buf);
                if (*buf != '*') {
                    *buf = '\0';
                    BNF_ERR("quantifier %d not proceeded by '*'\n", atoi(m));
                    RETURN_ERR(num_without_star);
                }
                *buf = '\0';
                token->min = atoi(m);
            }
            buf++;
            char* n = buf;
            buf = get_next_unmatching(&parsers.num, buf);
            if (n == buf) {
                // no max
                token->max = -1;
            }
            else {
                token->max = atoi(n);
            }
            if (token->min == 0 && token->max == 0) {
                BNF_ERR("not allowed to have 0-quantity rule\n");
                RETURN_ERR(zero_quantifier);
            }
            if (token->max != -1 && token->min > token->max) {
                BNF_ERR("max cannot be greater than min in quantifier rule "
                        "(found %d*%d)\n", token->min, token->max);
                RETURN_ERR(zero_quantifier);
            }
            buf = get_next_unmatching(&parsers.whitespace, buf);
            if (*buf == '\0') {
                BNF_ERR("no token following '*' quantifier\n");
                RETURN_ERR(no_token_after_quantifier);
            }
        }

        // check for groupings
        state->buf = buf;
        if (*buf == '{') {
            // skip over this group identifier
            state->buf++;
            // capturing group
            token_t *ret = token_group_parse(state, '}');
            if (ret == NULL) {
                PROPAGATE_ERR;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->flags |= TOKEN_CAPTURE;
            token->match_idx = state->n_captures++;

            token->node = (pattern_t*) ret;
            patt_ref_inc((pattern_t*) ret);
            // connect ret back to token
            pattern_connect(ret, token);
        }
        else if (*buf == '[') {
            if (token->min != 0 || token->max != 0) {
                BNF_ERR("not allowed to quantify optional group []\n");
                RETURN_ERR(overspecified_quantifier);
            }
            // skip over this group identifier
            state->buf++;
            // optional group
            token_t *ret = token_group_parse(state, ']');
            if (ret == NULL) {
                PROPAGATE_ERR;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->max = 1;
            token->node = (pattern_t*) ret;
            patt_ref_inc((pattern_t*) ret);
            // connect ret back to token
            pattern_connect(ret, token);
        }
        else if (*buf == '(') {
            // skip over this group identifier
            state->buf++;
            // plain group
            token_t *ret = token_group_parse(state, ')');
            if (ret == NULL) {
                PROPAGATE_ERR;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->node = (pattern_t*) ret;
            patt_ref_inc((pattern_t*) ret);
            // connect ret back to token
            pattern_connect(ret, token);
        }
        else if (*buf == '"') {
            // literal
            // skip first double quote char
            char *word = ++buf;
            // search for next double quote not immediately preceeded by a
            // backslash (between contains the whole word)
            buf = get_next_unescaped(&parsers.quote, buf);
            if (*buf == '\0') {
                BNF_ERR("string not terminated\n");
                RETURN_ERR(open_string);
            }
            *buf = '\0';
            buf++;

            // update state buffer, which is assumed to hold the current
            // position at the end of these ifs
            state->buf = buf;

            // length does not include the null terminator
            size_t word_len = (size_t) (buf - word) - 1;
            if (word_len == 0) {
                BNF_ERR("string literal cannot be empty\n");
                RETURN_ERR(empty_string);
            }

            pattern_t *lit = make_literal(word_len);
            memcpy(lit->lit.word, word, word_len);

            token->node = lit;
            patt_ref_inc(lit);
        }
        else if (*buf == '\'') {
            // single-character literal

            // ignore the first '
            buf++;

            char val;
            if (*buf == '\0') {
                // badly-formatted single-character literal
                BNF_ERR("dangling \"'\" at end of line\n");
                free(token);
                RETURN_ERR(bad_single_char_lit);
            }
            else if (*buf == '\\') {
                // escaped literal
                buf++;
                
                if (*buf == '\0') {
                    BNF_ERR("dangling \"\\\" at end of line\n");
                    RETURN_ERR(bad_single_char_lit);
                }
                else if (*(buf + 1) == '\'') {

                    // escaped literal char
                    switch (*buf) {
                        case 'a':
                            val = '\a';
                            break;
                        case 'b':
                            val = '\b';
                            break;
                        case 'f':
                            val = '\f';
                            break;
                        case 'n':
                            val = '\n';
                            break;
                        case 'r':
                            val = '\r';
                            break;
                        case 't':
                            val = '\t';
                            break;
                        case 'v':
                            val = '\v';
                            break;
                        case '\\':
                            val = '\\';
                            break;
                        case '\'':
                            val = '\'';
                            break;
                        case '"':
                            val = '\"';
                            break;
                        case '?':
                            val = '\?';
                            break;
                        default:
                            BNF_ERR("unknown escape sequence \"\\%c\"",
                                    *buf);
                            RETURN_ERR(bad_single_char_lit);
                    }
                    // place buf just beyond the last '
                    buf += 2;
                }
                else if (*buf == 'x' && *(buf + 1) != '\0'
                        && *(buf + 2) != '\0' && *(buf + 3) == '\'') {

                    buf++;

#define IS_HEX(c) (((c) >= '0' && (c) <= '9') \
        || ((c) >= 'a' && (c) <= 'f') \
        || ((c) >= 'A' && (c) <= 'F'))

                    // escaped hex char

                    if (!IS_HEX(*buf) || !IS_HEX(*(buf + 1))) {
                        BNF_ERR("invalid char hexcode \"\\x%c%c\"\n",
                                *buf, *(buf + 1));
                        RETURN_ERR(bad_single_char_lit);
                    }

#undef IS_HEX

#define HEX_VAL(c) \
                    (((c) >= '0' && (c) <= '9') ? (c) - '0' : \
                     ((c) >= 'a' && (c) <= 'f') ? (c) - 'a' + 10 : \
                     (c) - 'A' + 10)
                    
                    val = (HEX_VAL(*buf) << 4) + HEX_VAL(*(buf + 1));

                    // place buf just beyond the last '
                    buf += 3;
                }
                else {
                    BNF_ERR("badly formatted single-character literal\n");
                    RETURN_ERR(bad_single_char_lit);
                }
            }
            else if (*(buf + 1) == '\'') {
                // unescaped single-char literal
                val = *buf;

                if (val == '\'') {
                    BNF_ERR("cannot have empty literal ''\n");
                    RETURN_ERR(bad_single_char_lit);
                }

                // place buf just beyond the last '
                buf += 2;
            }
            else {
                BNF_ERR("badly formatted single-character literal\n");
                RETURN_ERR(bad_single_char_lit);
            }
            state->buf = buf;

            pattern_t *lit = make_literal(1);
            lit->lit.word[0] = val;

            token->node = lit;
            patt_ref_inc(lit);
        }
        else if (*buf == ';') {
            // comment: skip all characters until we reach the null terminator
            // at the end of the line
            state->buf = get_next_unmatching(&parsers.all, buf);
            free(token);
            continue;
        }
        else {
            // check for a plain token
            char* name = buf;
            buf = skip_unreserved(buf);
            if (name == buf) {
                BNF_ERR("unexpected token \"%c\" (0x%x)\n", *buf, *buf);
                RETURN_ERR(unexpected_token);
            }

            // update state buffer, which is assumed to hold the current
            // position at the end of these ifs
            state->buf = buf;

            // length, not including the null terminater
            size_t word_len = (size_t) (buf - name);
            // copy the name from the buffer into a malloced region pointed
            // to from the rule_list_node
            pattern_t *unres = make_literal(word_len);
            unres->type = TYPE_UNRESOLVED;
            memcpy(unres->lit.word, name, word_len);

            token->node = unres;
            patt_ref_inc(unres);
        }

        buf = state->buf;

        if (term_on != '\0') {
            // parenthesis, etc. allow crossing lines
            retval = get_next_non_whitespace(state);
            if (retval != 0) {
                // EOF reached, illegal condition
                BNF_ERR("EOF reached while in enclosed group (either \"()\", "
                        "\"{}\" or \"[]\")\n");
                RETURN_ERR(unclosed_grouping);
            }
            buf = state->buf;
        }
        else {
            // if this is not within a nested group, don't allow crossing
            // over lines
            buf = get_next_unmatching(&parsers.whitespace, buf);
        }

        if (token->min == 0 && token->max == 0) {
            // if min and max have not been set, then set them both to the
            // default
            token->min = token->max = 1;
        }

        // check to make sure that, in an OR grouping, there is a | after the
        // token just processed (or it is the end of the line)
        if (rule_grouping == GROUP_OR) {
            // in an or group
            if (*buf != term_on /*&& *buf != '\0'*/ && *buf != '|') {
                BNF_ERR("missing '|' between tokens in an OR grouping, if "
                        "the two are to be interleaved, group with "
                        "parenthesis the ORs and ANDs separately\n");
                RETURN_ERR(and_or_mix);
            }
            // insert in list of nodes
            pattern_or(last, token);
        }
        else if (rule_grouping == GROUP_AND) {
            // in an AND group, check for badly-placed '|'
            if (*buf == '|') {
                // found | in an AND environment
                BNF_ERR("found '|' after tokens in an AND grouping, if "
                        "the two are to be interleaved, group with "
                        "parenthesis the ORs and ANDs separately\n");
                RETURN_ERR(and_or_mix);
            }
            // insert in list of nodes
            pattern_connect(last, token);
        }
        else {
            // by default, go with AND, even if there is only one token
            rule_grouping = (*buf == '|') ? GROUP_OR : GROUP_AND;
        }
        // skip over an or character if there is one
        if (*buf == '|') {
            buf++;
        }
        state->buf = buf;

        last = token;
        if (ret == NULL) {
            ret = token;
        }

    } while (*buf != term_on);

    return ret;
}



/*
 *
 * file is open FILE* for cnf file
 * linen is the line number we are on in the file (for error reporting)
 *
 * this method returns the parsed rule on success, otherwise NULL and errno
 * is set accordingly
 */
static token_t* rule_parse(parse_state *state) {
    int ret;

    char *name;

    // skip empty lines and comments
    do {
        ret = read_line(state);
        if (ret != 0) {
            errno = ret;
            return NULL;
        }
        state->buf = skip_whitespace(state->buf);
        if (*(state->buf) == ';') {
            state->buf = get_next_unmatching(&parsers.all, state->buf);
        }
        int *a = (int*) malloc(sizeof(int));
        *a = 2;
        free(a);
    } while (*(state->buf) == '\0');

    char *buf = state->buf;
    // buf is pointing to the beginning of the first rule
    
    if (*buf == '=') {
        // = appeared before a name
        BNF_ERR("rule does not have a name\n");
        errno = rule_without_name;
        return NULL;
    }
    name = buf;
    buf = skip_unreserved(name);

    if (*buf == '=') {
        *buf = '\0';
    }
    else if (*buf == '\0') {
        BNF_ERR("rule %s not proceeded by an \"=\"\n", buf);
        errno = rule_without_eq;
        return NULL;
    }
    else {
        *buf = '\0';
        buf++;
        buf = skip_whitespace(buf);
        if (*buf != '=') {
            BNF_ERR("rule %s not proceeded by an \"=\"\n", buf);
            errno = rule_without_eq;
            return NULL;
        }
        buf++;
        buf = skip_whitespace(buf);
    }

    state->buf = buf;
    // buf is now at the start of the tokens

    char* hash_name = (char*) malloc(strlen(name) + 1);
    if (hash_name == NULL) {
        return NULL;
    }
    strcpy(hash_name, name);

    token_t *rule = token_group_parse(state, '\0');
    if (rule == NULL) {
        free(hash_name);
        return NULL;
    }

    ret = hash_insert(&state->rules, hash_name, rule);
    if (ret != 0) {
        BNF_ERR("duplicate symbol %s\n", hash_name);
        free(hash_name);
        pattern_free(rule);
        return NULL;
    }

    return rule;
}



// place bits in tmp field of token_t's to denote when they are
// ancestors of the current token_t being resolved (PROCESSING) or
// when they have been resolved fully already (RESOLVED). This allows
// for detection of cycles (finding a processing node as a child of
// some other processing node) and for faster resolution (not checking
// all children of a token_t if it has been resolved once)
#define CLEAR_MASK 0x3
#define PROCESSING 0x1
#define VISITED    0x2


static void clear_processing_bits(token_t *token) {
    token->tmp &= ~CLEAR_MASK;
}

static int is_processing(token_t *token) {
    return (token->tmp & PROCESSING) != 0;
}

static void mark_processing(token_t *token) {
    token->tmp &= ~VISITED;
    token->tmp |= PROCESSING;
}

static int is_visited(token_t *token) {
    return (token->tmp & VISITED) != 0;
}

static void mark_visited(token_t *token) {
    token->tmp &= ~PROCESSING;
    token->tmp |= VISITED;
}


#define ANONYMOUS 1
#define NOT_ANONYMOUS 0

/*
 * helper method for resolve_symbols
 *
 * anonymous is 1 if this symbol is not in the hashmap, meaning we should not
 * mark it's processing bits (as they won't be reset, nor is it possible to
 * circularly reference an anonymous symbol which is what the processing bits
 * check for)
 */
static int _resolve_symbols(hashmap *rules, token_t *token, int depth,
        int anonymous) {

    int ret = 0;

    if (is_processing(token) || is_visited(token)) {
        // we have already visited this symbol, so its children have been
        // resolved
        return 0;
    }

    if (!anonymous) {
        mark_processing(token);
    }
    else {
        // for curcular references in tokens of tokens
        mark_visited(token);
    }

    // a resolved symbol, if node is TYPE_UNRESOLVED (so that it may be marked
    // as complete after recursing into node)
    token_t *res = NULL;

    if (patt_type(token->node) == TYPE_UNRESOLVED) {
        // if this child is unresolved, check the list of rules for a
        // match

        pattern_t *unres = token->node;
        unresolved *sym = &unres->lit;

        // must malloc because we need null terminator
        char *symbol = (char*) malloc(sym->length + 1);
        memcpy(symbol, sym->word, sym->length);
        symbol[sym->length] = '\0';

        res = hash_get(rules, symbol);

        if (res == NULL) {
            // if not found, this is an undefined symbol
            BNF_ERR_NOLINE("symbol \"%s\" undefined\n", symbol);
            errno = undefined_symbol;
            free(symbol);
            return -1;
        }
        free(symbol);

        if (is_processing(res)) {
            // error, cycle detected
            BNF_ERR_NOLINE("circular symbol reference\n");
            errno = circular_definition;
            return -1;
        }
        mark_processing(res);

        // place a copy of the symbol in place of the reference    
        token->node = (pattern_t*) pattern_deep_copy(res);
        clear_processing_bits(&token->node->token);
        patt_ref_inc(token->node);
        // and connect the copy back to token
        pattern_connect(&token->node->token, token);

        // we are now done with the unresolved node, so we can free it
        patt_ref_dec(unres);
        if (patt_ref_count(unres) == 0) {
            free(unres);
        }
    }
    if (patt_type(token->node) == TYPE_TOKEN) {
        ret = _resolve_symbols(rules, &token->node->token, depth + 1,
                ANONYMOUS);
    }
    if (res != NULL) {
        mark_visited(res);
    }

    if (ret == 0 && token->alt != NULL) {
        ret = _resolve_symbols(rules, token->alt, depth + 1,
                ANONYMOUS);
    }
    if (ret == 0 && token->next != NULL) {
        ret = _resolve_symbols(rules, token->next, depth + 1, ANONYMOUS);
    }

    if (!anonymous) {
        mark_visited(token);
    }
    else {
        clear_processing_bits(token);
    }
    return ret;
}


/*
 * recursively resolve symbol names, beginning from the main rule, until the
 * main rule is fully formed. If there are any unused symbols, a warning will
 * be printed saying so.
 *
 * on error, -1 is returned and errno is set
 */
static int resolve_symbols(parse_state *state) {

    int ret = _resolve_symbols(&state->rules, state->main_rule, 0,
            NOT_ANONYMOUS);

    // go through and check to see if there are any unused symbols
    void *k, *v;
    hashmap_for_each(&state->rules, k, v) {
        token_t *token = (token_t*) v;
        if (!is_visited(token)) {
            // unused symbol
            BNF_WARNING("unused symbol %s\n", (char*) k);
            pattern_free(token);
        }
        else {
            clear_processing_bits(token);
            if (token != state->main_rule) {
                pattern_free(token);
            }
        }
        // each key was allocated specifically for the hashmap
        free(k);
    }
    return ret;
}



/*
 * go from first_literal until "until", which are all assumed to be literals,
 * and combine them into one big literal, freeing each of the now unused
 * literals and tokens (besides the first token which can be re-used and given
 * back) and mallocing a new literal struct
 *
 * links the new literal struct back into the children list by resing
 * first_literal token, so we don't have to worry about linking the predecessor
 * to first_literal, and we just need to link first_literal to until
 *
static void merge_literals(struct token *first_literal, struct token *until) {
    if (first_literal->next == until) {
        // if we are merging a single literal, we are done
        return;
    }
    
    size_t total_length = 0;
    for (struct token *t = first_literal; t != until; t = t->next) {
        literal *lit = &t->node->lit;
        total_length += lit->length;
    }

    pattern_t *big_lit_p = make_literal(total_length);
    literal *big_lit = &big_lit_p->lit;

    total_length = 0;
    for (struct token *t = first_literal; t != until; t = t->next) {
        pattern_t *patt = t->node;
        literal *lit = &patt->lit;

        size_t size = lit->length;
        memcpy(big_lit->word + total_length, lit->word, size);
        total_length += size;

        patt_ref_dec(patt);
        if (patt_ref_count(patt) == 0) {
            free(patt);
        }
        // reuse first literal's token struct for the new big_lit
        if (t != first_literal) {
            free(t);
        }
    }

    first_literal->node = big_lit_p;
    first_literal->next = until;
    first_literal->min = 1;
    first_literal->max = 1;
    first_literal->flags = 0;
}

*/

/*
 * go from first_single_char until "until", assuming all tokens in this
 * sequence are single-character literals, and merge them into a single
 * char_class object
 *
 * links the new literal struct back into the children list by resing
 * first_literal token, so we don't have to worry about linking the predecessor
 * to first_literal, and we just need to link first_literal to until
 *
static void merge_single_chars(struct token *first_single_char,
        struct token *until) {
    if (first_single_char->next == until) {
        // don't merge a single character into a char_class
        return;
    }

    pattern_t *cc_patt = make_char_class();
    char_class *cc = &cc_patt->cc;

    for (struct token *t = first_single_char; t != until; t = t->next) {
        pattern_t *patt = t->node;
        
        if (patt_type(patt) == TYPE_CC) {
            // if this is a char_class, merge all chars from it into this
            // char_class
            cc_allow_from(cc, &patt->cc);
        }
        else {
            // otherwise, this is a single-char literal, so just add its
            // value to the char_class
            cc_allow(cc, patt->lit.word[0]);
        }

        patt_ref_dec(patt);
        if (patt_ref_count(patt) == 0) {
            free(patt);
        }
        // reuse first literal's token struct for the new big_lit
        if (t != first_single_char) {
            free(t);
        }
    }

    first_single_char->node = cc_patt;
    first_single_char->next = until;
    first_single_char->min = 1;
    first_single_char->max = 1;
    first_single_char->flags = 0;
}

*/

/*
 * helper method for consolidate
 *
static int _consolidate(struct token *token) {
    if (patt_type(token->node) != TYPE_PATTERN) {
        // only consolidate patterns
        return 0;
    }

    pattern_t *node = token->node;
    c_pattern *patt = &node->patt;

    for (struct token *child = patt->first; child != NULL;
            child = child->next) {
        _consolidate(child);
    }

    if (patt->join_type == PATTERN_MATCH_AND) {
        // keep track of all contiguous literals so they can be merged into
        // one large literal
        struct token *first_literal = NULL;

#define LIT_IS_MERGEABLE(token) \
        ((token) != NULL \
         && patt_type((token)->node) == TYPE_LITERAL \
         && ((token)->min == (token)->max) \
         && !token_captures(token))

        for (struct token *child = patt->first; child != NULL;
                child = child->next) {
            // for merging multiple adjacent literals
            // test not only if we haven't found a first literal yet, but
            // also that there is at least one other literal after that we
            // can merge with
            if (first_literal == NULL && LIT_IS_MERGEABLE(child)) {
                first_literal = child;
            }
            if (first_literal != NULL && !LIT_IS_MERGEABLE(child->next)) {
                merge_literals(first_literal, child->next);
                if (child->next == NULL) {
                    // we need to update last pointer of this pattern
                    patt->last = first_literal;
                }
                first_literal = NULL;
            }
        }

#undef LIT_IS_MERGEABLE
    }
    else { // PATTERN_MATCH_OR

        // keep track of all contiguous single-char matchings so they can be
        // merged into a single char_class
        struct token *first_c = NULL;

#define LIT_IS_MERGEABLE(token) \
        ((token) != NULL \
         && ((patt_type((token)->node) == TYPE_LITERAL \
                 && ((token)->node->lit.length == 1)) \
             || patt_type((token)->node) == TYPE_CC))

        for (struct token *child = patt->first; child != NULL;
                child = child->next) {
            // for merging multiple adjacent single-character matchings
            if (first_c == NULL && LIT_IS_MERGEABLE(child)) {
                first_c = child;
            }
            if (first_c != NULL && !LIT_IS_MERGEABLE(child->next)) {
                merge_single_chars(first_c, child->next);
                if (child->next == NULL) {
                    // we need to update last pointer of this pattern
                    patt->last = first_c;
                }
                first_c = NULL;
            }
        }

#undef LIT_IS_MERGEABLE
    }

    // if a pattern has only one child, try to elevate the child (do this after
    // above work in case above would cause this pattern to now only have one
    // child)
    if (patt->first == patt->last) {
        struct token *child = patt->first;

        if (child->min > 1 && token->max != 1) {
            // we cannot consolidate with a child if they require more than
            // one grouping and we allow more than one occurence (because,
            // for example, the divisibility of number of occurences of a
            // pattern may matter)
            return 0;
        }

        // now elevate the child and free the old pattern
        //
        // possibilities:
        // 1*1(m*n("x")) -> m*n("x")
        // m*n(0*1("x")) -> 0*n("x")
        // m*n(1*1("x")) -> m*n("x")
        token->min = token->min == 1 ? child->min
            : (child->min == 0 ? 0 : token->min);
        token->max = token->max == 1 ? child->max : token->max;
        token->node = child->node;

        // free the pattern we just consolidated if its reference count went
        // to 0
        patt_ref_dec(node);
        if (patt_ref_count(node) == 0) {
            free(node);
        }
    }

    return 0;
}

*/

/*
 * attempts to shorten the pattern tree by either merging multiple nested
 * patterns into one larger pattern or by turning multiple ORed characters
 * into a char_class, etc.
 *
 * returns 0 on success, -1 on failure
 *
static int consolidate(parse_state *state) {
    struct token _placeholder = {
        .node = state->main_rule,
        .min = 1,
        .max = 1,
        .flags = 0
    };
    int ret = _consolidate(&_placeholder);
    // in case it was updated in _consolidate
    state->main_rule = _placeholder.node;
    return ret;
}
*/


static void free_state(parse_state *state) {
    char *rule_name;
    token_t *token;
    hashmap_for_each(&state->rules, rule_name, token) {
        free(rule_name);
        pattern_free(token);
    }
    hash_free(&state->rules);
}

/*
 * initializes hashmap and then propagates calls to rule_parse
 */
static token_t* bnf_parse(parse_state *state) {
    if (hash_init(&state->rules, &str_hash, &str_cmp) != 0) {
        dprintf(STDERR_FILENO, "Unable to initialize hashmap\n");
        return NULL;
    }
    init_parsers();

    // first parse the main rule, which is defined as the first rule
    state->main_rule = rule_parse(state);
    if (state->main_rule == NULL) {
        hash_free(&state->rules);
        return NULL;
    }

    // and now parse the remaining rules
    while (rule_parse(state) != NULL);
    if (errno != eof) {
        free_state(state);
        return NULL;
    }
    // successfully parsed everything
    errno = 0;

    /*void *k, *v;
    hashmap_for_each(&state->rules, k, v) {
        printf("%s = ", (char*) k);
        bnf_print(v);
        printf("\n");
    }*/

    // try to recursively resolve all symbols
    int ret = resolve_symbols(state);
    if (ret != 0) {
        pattern_free(state->main_rule);
        state->main_rule = NULL;
    }
    else {
        ret = 0;//consolidate(state);
        if (ret != 0) {
            pattern_free(state->main_rule);
            state->main_rule = NULL;
        }
    }

    hash_free(&state->rules);
    return state->main_rule;
}


token_t* bnf_parsef(const char *bnf_path) {
    parse_state state = {
        .linen = 0,
        .main_rule = NULL,
        .buf = NULL,
        .read_from = PARSING_FILE,
        .n_captures = 0,
        .file_buf = NULL,
        .line_size = 0
    };

    state.file = fopen(bnf_path, "r");

    if (state.file == NULL) {
        dprintf(STDERR_FILENO, "Unable to open file %s\n", bnf_path);
        return NULL;
    }

    token_t *ret = bnf_parse(&state);

    if (state.file_buf != NULL) {
        free(state.file_buf);
    }

    fclose(state.file);
    return ret;
}


token_t* bnf_parseb(const char *buffer, size_t buf_size) {
    parse_state state = {
        .linen = 0,
        .main_rule = NULL,
        .buf = NULL,
        .read_from = PARSING_BUF,
        .n_captures = 0,
        .buffer = buffer,
        .segment = NULL,
        .buf_loc = 0,
        .buf_size = buf_size
    };

    token_t *ret = bnf_parse(&state);

    if (state.segment != NULL) {
        free(state.segment);
    }

    return ret;
}


/*static void _bnf_print(token_t *patt, int min, int max) {
    if (min == 0 && max == 1) {
        printf("[");
    }
    else if (min != 1 || max != 1) {
        if (min != 0) {
            printf("%d", min);
        }
        printf("*");
        if (max != -1) {
            printf("%d", max);
        }
        printf("(");
    }
    switch (patt_type(patt)) {
        case TYPE_PATTERN:
            if (min == 1 && max == 1) {
                printf("(");
            }
            for (struct token *t = patt->patt.first; t != NULL; t = t->next) {
                _bnf_print(t->node, t->min, t->max);
                if (t->next != NULL) {
                    printf((patt->patt.join_type == PATTERN_MATCH_AND)
                            ? " " : " | ");
                }
            }
            if (min == 1 && max == 1) {
                printf(")");
            }
            break;
        case TYPE_CC:
            printf("<");
            for (unsigned char c = 0; c < NUM_CHARS; c++) {
                if (cc_is_match(&patt->cc, c)) {
                    printf("%c", c);
                }
            }
            printf(">");
            break;
        case TYPE_LITERAL:
            if (patt->lit.length == 1) {
                printf("'%c'", patt->lit.word[0]);
            }
            else {
                printf("\"%.*s\"", patt->lit.length, patt->lit.word);
            }
            break;
        case TYPE_UNRESOLVED:
            printf("%.*s", patt->lit.length, patt->lit.word);
            break;
    }
    if (min == 0 && max == 1) {
        printf("]");
    }
    else if (min != 1 || max != 1) {
        printf(")");
    }
}*/

static unsigned count_;

static void _bnf_print(token_t *patt, hashmap *seen) {
    char *buf;
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

    printf("p%u: %d*%d (tmp = %d) (r = %d)", *(unsigned*) hash_get(seen, patt),
            patt->min, patt->max, patt->tmp, patt_ref_count((pattern_t*) patt));
    if (token_captures(patt)) {
        printf("(mid: %u) ", patt->match_idx);
    }
    printf("\t");
    switch (patt_type(patt->node)) {
        case TYPE_CC:
            printf("<>");
            break;
        case TYPE_LITERAL:
            lit = &patt->node->lit;
            buf = (char*) malloc(lit->length + 1);
            memcpy(buf, lit->word, lit->length);
            buf[lit->length] = '\0';
            printf("\"%s\"", buf);
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

void bnf_print(token_t *patt) {
    count_ = 0;
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);
    _bnf_print(patt, &seen);
    printf("\n");

    void *k;
    unsigned *count;
    hashmap_for_each(&seen, k, count) {
        free(count);
    }
    hash_free(&seen);
}


