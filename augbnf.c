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
    vprintf(P_RED "BNF compiler error" P_YELLOW " (line %lu): " \
            P_RESET msg, state->linen, ## __VA_ARGS__)

#define BNF_ERR_NOLINE(msg, ...) \
    vprintf(P_RED "BNF compiler error: " \
            P_RESET msg, ## __VA_ARGS__)

#define BNF_WARNING(msg, ...) \
    vprintf(P_YELLOW "BNF compiler warning: " P_RESET msg, ## __VA_ARGS__)



// static globals which hold character classes used in parsing
static struct parsers {
    char_class whitespace;
    char_class alpha;
    char_class num;
    char_class alphanum;
    char_class quote;
} parsers;

// initializes the parsers, to be called once at the beginning of a bnf_parse
// call
static __inline void init_parsers() {
    __builtin_memset(&parsers, 0, sizeof(struct parsers));

    cc_allow_whitespace(&parsers.whitespace);
    cc_allow_alpha(&parsers.alpha);
    cc_allow_num(&parsers.num);
    cc_allow_alphanum(&parsers.alphanum);
    cc_allow(&parsers.quote, '"');
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
static char* skip_alphanum(char* buf) {
    while (cc_is_match(&parsers.alphanum, *buf)) {
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
    pattern_t *main_rule;

    // the pointer share between recursive calls of where in memory we are
    // currently parsing the bfn
    char *buf;

    // we can either be reading from an open file or from a buffer in memory
    // either PARSING_FILE or PARSING_BUF
    int read_from;
    union {
        struct {
            FILE *file;
            // pointer to the beginning of the memory region read into from
            // the file, which must not be modified outside of getline for
            // proper memory cleanup
            char *file_buf;
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
    size_t line_size;
    int ret;

    if (state->read_from == PARSING_FILE) {
        ret = getline(&state->file_buf, &line_size, state->file);
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
 */
static int token_group_parse(parse_state *state, c_pattern *rule,
        char term_on) {

    char *buf = state->buf;
    int ret;

    // to be set when it is known whether they are using PATTERN_AND
    // or PATTERN_OR grouping
    int determined_rule_grouping = 0;

    struct token *token;
    pattern_t *group;

    do {
        ret = get_next_non_whitespace(state);
        if (ret != 0) {
            // reached EOF
            return ret;
        }

        // since the preceeding method modified only buf in state
        buf = state->buf;

        token = make_token();

        // search for quantifiers
        if (cc_is_match(&parsers.num, *buf)) {
            // expect something of the form <m>* or <m>*<n>
            char* m = buf;
            buf = get_next_unmatching(&parsers.num, buf);
            if (*buf != '*') {
                *buf = '\0';
                BNF_ERR("quantifier %d not proceeded by '*'\n", atoi(m));
                free(token);
                return num_without_star;
            }
            *buf = '\0';
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
            token->min = atoi(m);
            if (token->min == 0 && token->max == 0) {
                BNF_ERR("not allowed to have 0-quantity rule\n");
                free(token);
                return zero_quantifier;
            }
            if (token->max != -1 && token->min > token->max) {
                BNF_ERR("max cannot be greater than min in quantifier rule "
                        "(found %d*%d)\n", token->min, token->max);
                free(token);
                return zero_quantifier;
            }
            buf = get_next_unmatching(&parsers.whitespace, buf);
            if (*buf == '\0') {
                BNF_ERR("no token following '*' quantifier\n");
                free(token);
                return no_token_after_quantifier;
            }
        }

        // check for groupings
        state->buf = buf;
        if (*buf == '{') {
            // skip over this group identifier
            state->buf++;
            // capturing group
            group = make_c_pattern();
            group->type |= PATT_ANONYMOUS;
            ret = token_group_parse(state, &group->patt, '}');
            if (ret != 0) {
                free(group);
                free(token);
                return ret;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->flags |= TOKEN_CAPTURE;
            token->node = group;
        }
        else if (*buf == '[') {
            if (token->min != 0 || token->max != 0) {
                BNF_ERR("not allowed to quantify optional group []\n");
                free(token);
                return overspecified_quantifier;
            }
            // skip over this group identifier
            state->buf++;
            // optional group
            group = make_c_pattern();
            group->type |= PATT_ANONYMOUS;
            ret = token_group_parse(state, &group->patt, ']');
            if (ret != 0) {
                free(group);
                free(token);
                return ret;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->max = 1;
            token->node = group;
        }
        else if (*buf == '(') {
            // skip over this group identifier
            state->buf++;
            // plain group
            group = make_c_pattern();
            group->type |= PATT_ANONYMOUS;
            ret = token_group_parse(state, &group->patt, ')');
            if (ret != 0) {
                free(group);
                free(token);
                return ret;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->node = group;
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
                free(token);
                return open_string;
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
                free(token);
                return empty_string;
            }

            group = make_literal(word_len);
            group->type |= PATT_ANONYMOUS;
            memcpy(group->lit.word, word, word_len);

            token->node = group;
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
                return bad_single_char_lit;
            }

            if (*(buf + 1) == '\'') {
                // unescaped single-char literal
                val = *buf;

                // place buf just beyond the last '
                buf += 2;
            }
            else if (*buf == '\\') {
                buf++;
                
                if (*buf == '\0') {
                    BNF_ERR("dangling \"\\\" at end of line\n");
                    free(token);
                    return bad_single_char_lit;
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
                            free(token);
                            return bad_single_char_lit;
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
                        free(token);
                        return bad_single_char_lit;
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
                    free(token);
                    return bad_single_char_lit;
                }
            }
            else {
                BNF_ERR("badly formatted single-character literal\n");
                free(token);
                return bad_single_char_lit;
            }
            state->buf = buf;

            group = make_literal(1);
            group->type |= PATT_ANONYMOUS;
            group->lit.word[0] = val;

            token->node = group;
        }
        else {
            // check for a plain token
            char* name = buf;
            buf = get_next_unmatching(&parsers.alphanum, buf);
            if (name == buf) {
                BNF_ERR("unexpected token \"%c\" (0x%x)\n", *buf, *buf);
                free(token);
                return unexpected_token;
            }

            // update state buffer, which is assumed to hold the current
            // position at the end of these ifs
            state->buf = buf;

            // length, not including the null terminater
            size_t word_len = (size_t) (buf - name);
            // copy the name from the buffer into a malloced region pointed
            // to from the rule_list_node
            group = make_literal(word_len);
            group->type = TYPE_UNRESOLVED | PATT_ANONYMOUS;
            memcpy(group->lit.word, name, word_len);

            token->node = group;
        }
        // under all branches, we created a new pattern_t node, so increment
        // its reference count
        patt_ref_inc(group);

        buf = state->buf;

        if (term_on != '\0') {
            // parenthesis, etc. allow crossing lines
            ret = get_next_non_whitespace(state);
            if (ret != 0) {
                // EOF reached, illegal condition
                BNF_ERR("EOF reached while in enclosed group (either \"()\", "
                        "\"{}\" or \"[]\")\n");
                // only safe because node is first element of token
                pattern_free(token->node);
                return unclosed_grouping;
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
        if (determined_rule_grouping) {
            if (rule->join_type == PATTERN_MATCH_OR) {
                // in an or group
                if (*buf != term_on && *buf != '\0' && *buf != '|') {
                    BNF_ERR("missing '|' between tokens in an OR grouping, if "
                            "the two are to be interleaved, group with "
                            "parenthesis the ORs and ANDs separately\n");
                    pattern_free(token->node);
                    return and_or_mix;
                }
            }
            else {
                // in an AND group, check for badly-placed '|'
                if (*buf == '|') {
                    // found | in an AND environment
                    BNF_ERR("found '|' after tokens in an AND grouping, if "
                            "the two are to be interleaved, group with "
                            "parenthesis the ORs and ANDs separately\n");
                    pattern_free(token->node);
                    return and_or_mix;
                }
            }
        }
        else {
            // by default, go with AND, even if there is only one token
            rule->join_type = (*buf == '|') ? PATTERN_MATCH_OR
                : PATTERN_MATCH_AND;
            determined_rule_grouping = 1;
        }
        // skip over an or character if there is one
        if (*buf == '|') {
            buf++;
        }
        state->buf = buf;

        // insert node into list of nodes in rule
        pattern_insert(rule, token);

    } while (*buf != term_on);

    return 0;
}



/*
 *
 * file is open FILE* for cnf file
 * linen is the line number we are on in the file (for error reporting)
 *
 * this method returns the parsed rule on success, otherwise NULL and errno
 * is set accordingly
 */
static pattern_t* rule_parse(parse_state *state) {
    int ret;

    char *name;

    // skip empty lines
    do {
        ret = read_line(state);
        if (ret != 0) {
            errno = ret;
            return NULL;
        }
        state->buf = skip_whitespace(state->buf);
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
    buf = skip_alphanum(name);

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

    pattern_t *rule = make_c_pattern();
    if (rule == NULL) {
        return NULL;
    }

    char* hash_name = (char*) malloc(strlen(name) + 1);
    if (hash_name == NULL) {
        free(rule);
    }
    strcpy(hash_name, name);
    hash_insert(&state->rules, hash_name, rule);

    ret = token_group_parse(state, &rule->patt, '\0');
    if (ret != 0) {
        errno = ret;
        // memory cleanup will be done outside, as rule is already in rule set
        return NULL;
    }
    // increment reference count of this rule to 1
    patt_ref_inc(rule);
    return rule;
}




// place bits in join_type field of c_patterns to denote when they are
// ancestors of the current c_pattern being resolved (PROCESSING) or
// when they have been resolved fully already (RESOLVED). This allows
// for detection of cycles (finding a processing node as a child of
// some other processing node) and for faster resolution (not checking
// all children of a c_pattern if it has been resolved once)
#define CLEAR_MASK 0x30
#define PROCESSING 0x10
#define VISITED    0x20


static void clear_processing_bits(c_pattern *rule) {
    rule->join_type &= ~CLEAR_MASK;
}

static int is_processing(c_pattern *rule) {
    return (rule->join_type & PROCESSING) != 0;
}

static void mark_processing(c_pattern *rule) {
    // we ONLY want to mark rules that are not anonymous (i.e. in the map of
    // rules), since anonymous rules cannot be referenced by more than one
    // other rule and because we only go back and clear the processing bits
    // of named rules
    if (!patt_anonymous(CPATT_TO_PATT(rule))) {
        rule->join_type |= PROCESSING;
    }
}

static int is_visited(c_pattern *rule) {
    return (rule->join_type & VISITED) != 0;
}

static void mark_visited(c_pattern *rule) {
    if (!patt_anonymous(CPATT_TO_PATT(rule))) {
        rule->join_type &= ~PROCESSING;
        rule->join_type |= VISITED;
    }
}


/*
 * helper method for resolve_symbols
 */
static int _resolve_symbols(hashmap *rules, pattern_t *rule) {

    if (patt_type(rule) != TYPE_PATTERN) {
        // only patterns can reference other symbols
        return 0;
    }

    c_pattern *patt = &rule->patt;

    if (is_processing(patt)) {
        // error, cycle detected
        BNF_ERR_NOLINE("circular symbol reference\n");
        errno = circular_definition;
        return -1;
    }
    if (is_visited(patt)) {
        // we have already visited this symbol, so its children have been
        // resolved
        return 0;
    }

    mark_processing(patt);

    for (struct token *child = patt->first; child != NULL;
            child = child->next) {

        if (patt_type(child->node) == TYPE_UNRESOLVED) {
            // if this child is unresolved, check the list of rules for a
            // match
            
            pattern_t *ch = child->node;
            unresolved *sym = &ch->lit;

            // must malloc because we need null terminator
            char *symbol = (char*) malloc(sym->length + 1);
            memcpy(symbol, sym->word, sym->length);
            symbol[sym->length] = '\0';

            pattern_t *res = hash_get(rules, symbol);

            if (res == NULL) {
                // if not found, this is an undefined symbol
                BNF_ERR_NOLINE("symbol \"%s\" undefined\n", symbol);
                errno = undefined_symbol;
                return -1;
            }

            int ret = _resolve_symbols(rules, res);
            if (ret != 0) {
                return ret;
            }

            // now place the resolved symbol in place of the old symbol
            child->node = res;

            // increment the reference count of what we just linked to
            patt_ref_inc(res);

            // we are now done with the unresolved node, so we can free it
            free(ch);
        }
        else if (patt_type(child->node) == TYPE_PATTERN) {
            // if this is a pattern, must make recursive calls to resolve
            int ret = _resolve_symbols(rules, child->node);
            if (ret != 0) {
                return ret;
            }
        }
    }

    mark_visited(patt);
    return 0;
}


/*
 * recursively resolve symbol names, beginning from the main rule, until the
 * main rule is fully formed. If there are any unused symbols, a warning will
 * be printed saying so.
 *
 * on error, -1 is returned and errno is set
 */
static int resolve_symbols(parse_state *state) {

    int ret = _resolve_symbols(&state->rules, state->main_rule);
    if (ret != 0) {
        return ret;
    }

    // go through and check to see if there are any unused symbols
    void *k, *v;
    hashmap_for_each(&state->rules, k, v) {
        pattern_t *node = (pattern_t*) v;
        if (patt_type(node) != TYPE_PATTERN) {
            // skip over non-patterns
            continue;
        }
        c_pattern *patt = &node->patt;
        if (!is_visited(patt)) {
            // unused symbol
            BNF_WARNING("unused symbol %s\n", (char*) k);
            pattern_free_shallow(node);
        }
        else {
            clear_processing_bits(patt);
        }
    }
    return 0;
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
 */
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


/*
 * go from first_single_char until "until", assuming all tokens in this
 * sequence are single-character literals, and merge them into a single
 * char_class object
 *
 * links the new literal struct back into the children list by resing
 * first_literal token, so we don't have to worry about linking the predecessor
 * to first_literal, and we just need to link first_literal to until
 */
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


/*
 * helper method for consolidate
 */
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


/*
 * attempts to shorten the pattern tree by either merging multiple nested
 * patterns into one larger pattern or by turning multiple ORed characters
 * into a char_class, etc.
 *
 * returns 0 on success, -1 on failure
 */
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



/*
 * initializes hashmap and then propagates calls to rule_parse
 */
static pattern_t* bnf_parse(parse_state *state) {
    if (hash_init(&state->rules, &str_hash, &str_cmp) != 0) {
        dprintf(STDERR_FILENO, "Unable to initialize hashmap\n");
        return NULL;
    }
    init_parsers();

    // first parse the main rule, which is defined as the first rule
    state->main_rule = rule_parse(state);
    if (state->main_rule == NULL) {
        return NULL;
    }

    // and now parse the remaining rules
    while (rule_parse(state) != NULL);
    if (errno != eof) {
        // TODO memory cleanup
        return NULL;
    }
    // successfully parsed everything
    errno = 0;

    // try to recursively resolve all symbols
    int ret = resolve_symbols(state);
    if (ret != 0) {
        pattern_free(state->main_rule);
        state->main_rule = NULL;
    }
    else {
        ret = consolidate(state);
        if (ret != 0) {
            pattern_free(state->main_rule);
            state->main_rule = NULL;
        }
    }

    hash_free(&state->rules);
    return state->main_rule;
}


pattern_t* bnf_parsef(const char *bnf_path) {
    parse_state state = {
        .linen = 0,
        .main_rule = NULL,
        .buf = NULL,
        .read_from = PARSING_FILE,
    };

    state.file = fopen(bnf_path, "r");

    if (state.file == NULL) {
        dprintf(STDERR_FILENO, "Unable to open file %s\n", bnf_path);
        return NULL;
    }

    pattern_t *ret = bnf_parse(&state);

    if (state.file_buf != NULL) {
        free(state.file_buf);
    }

    fclose(state.file);
    return ret;
}


pattern_t* bnf_parseb(const char *buffer, size_t buf_size) {
    parse_state state = {
        .linen = 0,
        .main_rule = NULL,
        .buf = NULL,
        .read_from = PARSING_BUF,
        .buffer = buffer,
        .segment = NULL,
        .buf_loc = 0,
        .buf_size = buf_size
    };

    pattern_t *ret = bnf_parse(&state);

    if (state.segment != NULL) {
        free(state.segment);
    }

    return ret;
}



static void _bnf_print(pattern_t *patt, int min, int max) {
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
                printf("\"%*s\"", patt->lit.length, patt->lit.word);
            }
            break;
        case TYPE_UNRESOLVED:
            printf("%*s", patt->lit.length, patt->lit.word);
            break;
    }
    if (min == 0 && max == 1) {
        printf("]");
    }
    else if (min != 1 || max != 1) {
        printf(")");
    }
}

void bnf_print(pattern_t *patt) {
    _bnf_print(patt, 1, 1);
    printf("\n");
}


