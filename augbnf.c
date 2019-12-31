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
    c_pattern *group;
    literal *lit;

    do {
        // must update buf in state since its value is used in the following
        // method
        state->buf = buf;
        ret = get_next_non_whitespace(state);
        if (ret != 0) {
            // reached EOF
            return ret;
        }

        // since the preceeding method modified only buf in state
        buf = state->buf;

        token = (struct token*) calloc(1, sizeof(struct token));

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
            group = (c_pattern*) calloc(1, sizeof(c_pattern));
            ret = token_group_parse(state, group, '}');
            if (ret != 0) {
                free(group);
                free(token);
                return ret;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->flags |= TOKEN_CAPTURE;
            token->node.patt = group;
            token->node.type = TYPE_PATTERN | PATT_ANONYMOUS;
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
            group = (c_pattern*) calloc(1, sizeof(c_pattern));
            ret = token_group_parse(state, group, ']');
            if (ret != 0) {
                free(group);
                free(token);
                return ret;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->max = 1;
            token->node.patt = group;
            token->node.type = TYPE_PATTERN | PATT_ANONYMOUS;
        }
        else if (*buf == '(') {
            // skip over this group identifier
            state->buf++;
            // plain group
            group = (c_pattern*) calloc(1, sizeof(c_pattern));
            ret = token_group_parse(state, group, ')');
            if (ret != 0) {
                free(group);
                free(token);
                return ret;
            }
            // skip over the terminating group identifier
            state->buf++;

            token->node.patt = group;
            token->node.type = TYPE_PATTERN | PATT_ANONYMOUS;
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

            // length includes the null terminator
            size_t word_len = (size_t) (buf - word);
            if (word_len == 1) {
                BNF_ERR("string literal cannot be empty\n");
                free(token);
                return empty_string;
            }

            lit = (literal*) malloc(word_len);
            memcpy(lit->word, word, word_len);

            token->node.lit = lit;
            token->node.type = TYPE_LITERAL | PATT_ANONYMOUS;
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

            // length, not including the null terminating character
            size_t word_len = (size_t) (buf - name);
            // copy the name from the buffer into a malloced region pointed
            // to from the rule_list_node
            token->node.lit = (unresolved*) malloc(word_len + 1);
            memcpy(token->node.lit->word, name, word_len);
            token->node.lit->word[word_len] = '\0';
            token->node.type = TYPE_UNRESOLVED | PATT_ANONYMOUS;
        }
        buf = state->buf;

        if (term_on != '\0') {
            // parenthesis, etc. allow crossing lines
            ret = get_next_non_whitespace(state);
            if (ret != 0) {
                // EOF reached, illegal condition
                BNF_ERR("EOF reached while in enclosed group (either \"()\", "
                        "\"{}\" or \"[]\")\n");
                // only safe because node is first element of token
                pattern_free(&token->node);
                return unclosed_grouping;
            }
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
                if (*buf != '\0' && *buf != '|') {
                    BNF_ERR("missing '|' between tokens in an OR grouping, if "
                            "the two are to be interleaved, group with "
                            "parenthesis the ORs and ANDs separately\n");
                    pattern_free(&token->node);
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
                    pattern_free(&token->node);
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

        // insert node into list of nodes in rule
        pattern_insert(rule, token);

    } while (*buf != term_on);

    state->buf = buf;

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

    pattern_t *rule = (pattern_t*) malloc(sizeof(pattern_t));
    if (rule == NULL) {
        return NULL;
    }
    rule->patt = (c_pattern*) calloc(1, sizeof(c_pattern));
    if (rule->patt == NULL) {
        free(rule);
        return NULL;
    }
    rule->type = TYPE_PATTERN;

    char* hash_name = (char*) malloc(strlen(name) + 1);
    if (hash_name == NULL) {
        free(rule->patt);
        free(rule);
    }
    strcpy(hash_name, name);
    hash_insert(&state->rules, hash_name, rule);

    ret = token_group_parse(state, rule->patt, '\0');
    if (ret != 0) {
        errno = ret;
        // memory cleanup will be done outside, as rule is already in rule set
        return NULL;
    }
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


static void clear_processing_bits(pattern_t *rule) {
    rule->type &= ~CLEAR_MASK;
}

static int is_processing(pattern_t *rule) {
    return (rule->type & PROCESSING) != 0;
}

static void mark_processing(pattern_t *rule) {
    rule->type |= PROCESSING;
}

static int is_visited(pattern_t *rule) {
    return (rule->type & VISITED) != 0;
}

static void mark_visited(pattern_t *rule) {
    rule->type &= ~PROCESSING;
    rule->type |= VISITED;
}


/*
 * helper method for resolve_symbols
 */
static int _resolve_symbols(hashmap *rules, pattern_t *rule) {

    if (is_processing(rule)) {
        // error, cycle detected
        BNF_ERR_NOLINE("circular symbol references\n");
        errno = circular_definition;
        return -1;
    }
    if (is_visited(rule)) {
        // we have already visited this symbol, so its children have been
        // resolved
        return 0;
    }

    if (patt_type(rule) != TYPE_PATTERN) {
        // only patterns can reference other symbols
        mark_visited(rule);
        return 0;
    }

    mark_processing(rule);
    c_pattern *patt = rule->patt;

    struct token *prev_child = NULL;
    for (struct token *child = patt->first; child != NULL;
            prev_child = child, child = child->next) {

        if (patt_type(&child->node) == TYPE_UNRESOLVED) {
            // if this child is unresolved, check the list of rules for a
            // match
            char *symbol = child->node.lit->word;
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

            unresolved *sym = child->node.lit;

            // now place the resolved symbol in place of the old symbol
            child->node = *res;

            // we are now done with the unresolved node, so we can free it
            free(sym);
        }
    }

    mark_visited(rule);
    return 0;
}


/*
 * recursively resolve symbol names, beginning from the main rule, until the
 * main rule is fully formed. If there are any unused symbols, a warning will
 * be printed saying so.
 *
 * On error, -1 is returned and errno is set
 */
static int resolve_symbols(parse_state *state) {

    int ret = _resolve_symbols(&state->rules, state->main_rule);
    if (ret != 0) {
        return ret;
    }
    
    // go through and check to see if there are any unused symbols
    void *k, *v;
    hashmap_for_each(&state->rules, k, v) {
        pattern_t *patt = (pattern_t*) v;
        if (!is_visited(patt)) {
            // unused symbol
            BNF_WARNING("unused symbol %s\n", (char*) k);
            pattern_free_shallow(patt);
        }
        else {
            clear_processing_bits(patt);
        }
    }
    return 0;
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

    state->main_rule = (pattern_t*) malloc(sizeof(pattern_t));
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



