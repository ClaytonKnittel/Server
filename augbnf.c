#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "augbnf.h"
#include "hashmap.h"
#include "vprint.h"
#include "util.h"


enum {
    success = 0,
    eof,
    rule_without_name,
    rule_without_eq,
    num_without_star,
    no_token_after_quantifier,
    unexpected_token,
    and_or_mix,
    overspecified_quantifier,
    zero_quantifier,
    open_string,
    empty_string,
};

// we add an additional type of node which fits in the patter_t struct which
// contains only a name and is to later be resolved when compiling the bnf
#define TYPE_UNRESOLVED 0x4

typedef literal unresolved;


#define BNF_ERR P_RED "BNF compiler error" P_YELLOW " (line " \
    TOSTRING(__LINE__) "): " P_RESET



/*
#define RESOLVED 0x1
#define UNRESOLVED 0x2
#define CAPTURING 0x4

#define PATTERN_AND 0x0
#define PATTERN_OR 0x1

typedef struct rule_list_node {
    // singly-linked list of rule_t objects
    struct rule *next;

    // name is NULL for anonymous tokens (i.e. ones that don't have to be
    // resolved)
    char *name;
    // options:
    //  either RESOLVED or UNRESOLVED
    //  CAPTURING or not
    int flags;

    // quantifier for rule, the default of 0, 0 means not yet defined
    struct {
        int min, max;
    };


    // exists only for resolved nodes
    union {
        struct {
            struct rule *rule;
            // shadows the type field in pattern_t, though type can also be
            // TYPE_RULE, to refer to the rule struct
            int type;
        };
        pattern_t *patt;
    };
} rule_list_node;*/

/*
 * each rule is intially just a name, a join type (and or or) and a linked
 * list of all rule names or rule_t objects in the pattern. Then, after symbol
 * resolution, the named rules are linked properly to their rule_t structs
 */
/*typedef struct rule {

    // types are:
    //  PATTERN_AND
    //  PATTERN_OR
    int type;

    // length of the linked list
    unsigned n_children;
    // singly linked list of rule_t objects, first and last pointers are both
    // present for convenience
    rule_list_node *first, *last;
} rule_t;
*/



static struct parsers {
    char_class whitespace;
    char_class alpha;
    char_class num;
    char_class quote;
} parsers;

static __inline void init_parsers() {
    __builtin_memset(&parsers, 0, sizeof(struct parsers));

    cc_allow_whitespace(&parsers.whitespace);
    cc_allow_alpha(&parsers.alpha);
    cc_allow_num(&parsers.num);
    cc_allow(&parsers.quote, '"');
}


static char* skip_whitespace(char* buf) {
    while (cc_is_match(&parsers.whitespace, *buf)) {
        buf++;
    }
    return buf;
}

static char* skip_alpha(char* buf) {
    while (cc_is_match(&parsers.alpha, *buf)) {
        buf++;
    }
    return buf;
}


typedef struct parse_state {
    hashmap rules;
    FILE *file;
    size_t linen;
    pattern_t *main_rule;
} parse_state;


typedef struct rule_lines {
    char *lines[MAX_LINES_PER_RULE];
    int n_lines;
} rule_lines;


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

// gets next non-whitespace character, reading more lines from the file if
// necessary
static char* get_next_non_whitespace(parse_state *state, rule_lines *lines,
        char* buf) {
    size_t line_size;
    int ret;

    buf = get_next_unmatching(&parsers.whitespace, buf);
    while (*buf == '\0') {
        ret = getline(&lines->lines[lines->n_lines], &line_size, state->file);
        state->linen++;
        if (ret == -1) {
            // EOF
            for (int i = 0; i <= lines->n_lines; i++) {
                free(lines->lines[lines->n_lines]);
            }
            return NULL;
        }
        buf = get_next_unmatching(&parsers.whitespace, buf);
    }
    return buf;
}

/*
 * parse all referenced tokens and literals out of the rule, stopping only
 * when the term_on character has been reached
 */
static int token_group_parse(parse_state *state, rule_lines *lines,
        c_pattern *rule, char **buf_ptr, char term_on) {

    char *buf = *buf_ptr;
    int ret;

    // to be set when it is known whether they are using PATTERN_AND
    // or PATTERN_OR grouping
    int determined_rule_grouping = 0;

    struct token *token;
    c_pattern *group;
    literal *lit;

    do {
        if (determined_rule_grouping && (rule->join_type == PATTERN_MATCH_OR)
                && *buf == '|') {
            buf++;
        }
        buf = get_next_non_whitespace(state, lines, buf);
        if (buf == NULL) {
            return eof;
        }

        token = (struct token*) calloc(1, sizeof(struct token));

        // search for quantifiers
        if (cc_is_match(&parsers.num, *buf)) {
            // expect something of the form <m>* or <m>*<n>
            char* m = buf;
            buf = get_next_unmatching(&parsers.num, buf);
            if (*buf != '*') {
                *buf = '\0';
                fprintf(stderr, BNF_ERR "quantifier %d not proceeded by '*'\n",
                        atoi(m));
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
                fprintf(stderr, BNF_ERR "not allowed to have 0-quantity "
                        "rule\n");
                return zero_quantifier;
            }
            if (token->max != -1 && token->min > token->max) {
                fprintf(stderr, BNF_ERR "max cannot be greater than min in "
                        "quantifier rule (found %d*%d)\n",
                        token->min, token->max);
                return zero_quantifier;
            }
            buf = get_next_unmatching(&parsers.whitespace, buf);
            if (*buf == '\0') {
                fprintf(stderr, BNF_ERR "no token following '*' quantifier\n");
                return no_token_after_quantifier;
            }
        }

        // check for groupings
        *buf_ptr = buf;
        if (*buf == '{') {
            // capturing group
            group = (c_pattern*) calloc(1, sizeof(c_pattern));
            ret = token_group_parse(state, lines, group, buf_ptr, '}');
            if (ret != 0) {
                return ret;
            }

            token->flags |= TOKEN_CAPTURE;
            token->node.patt = group;
            token->node.type = TYPE_PATTERN;
        }
        else if (*buf == '[') {
            if (token->min != 0 || token->max != 0) {
                fprintf(stderr, BNF_ERR "not allowed to quantify optional "
                        "group []\n");
                return overspecified_quantifier;
            }
            // optional group
            group = (c_pattern*) calloc(1, sizeof(c_pattern));
            ret = token_group_parse(state, lines, group, buf_ptr, ']');
            if (ret != 0) {
                return ret;
            }

            token->max = 1;
            token->node.patt = group;
            token->node.type = TYPE_PATTERN;
        }
        else if (*buf == '(') {
            // plain group
            group = (c_pattern*) calloc(1, sizeof(c_pattern));
            ret = token_group_parse(state, lines, group, buf_ptr, ')');
            if (ret != 0) {
                return ret;
            }

            token->node.patt = group;
            token->node.type = TYPE_PATTERN;
        }
        else if (*buf == '"') {
            // literal
            // skip first double quote char
            char *word = buf + 1;
            // search for next double quote not immediately preceeded by a
            // backslash (between contains the whole word)
            buf = get_next_unescaped(&parsers.quote, buf);
            if (*buf == '\0') {
                fprintf(stderr, BNF_ERR "string not terminated\n");
                return open_string;
            }
            *buf = '\0';
            buf++;

            // length includes the null terminator
            size_t word_len = (size_t) (buf - word);
            if (word_len == 1) {
                fprintf(stderr, BNF_ERR "string literal cannot be empty\n");
                return empty_string;
            }

            lit = (literal*) malloc(word_len);
            memcpy(lit->word, word, word_len);

            token->node.lit = lit;
            token->node.type = TYPE_LITERAL;
        }
        else {
            // check for a plain token
            char* name = buf;
            buf = get_next_unmatching(&parsers.alpha, buf);
            if (name == buf) {
                fprintf(stderr, BNF_ERR "unexpected token \"%c\" (0x%x)\n",
                        *buf, *buf);
                return unexpected_token;
            }

            // length, not including the null terminating character
            size_t word_len = (size_t) (buf - name);
            // copy the name from the buffer into a malloced region pointed
            // to from the rule_list_node
            token->node.lit = (unresolved*) malloc(word_len + 1);
            memcpy(token->node.lit->word, name, word_len);
            token->node.lit->word[word_len] = '\0';
            token->node.type = TYPE_UNRESOLVED;
        }

        if (term_on != '\0') {
            // parenthesis, etc. allow crossing lines
            buf = get_next_non_whitespace(state, lines, buf);
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
                    fprintf(stderr, BNF_ERR "missing '|' between tokens in an "
                            "OR grouping, if the two are to be interleaved, "
                            "group with parenthesis the ORs and ANDs "
                            "separately\n");
                    return and_or_mix;
                }
            }
            else {
                // in an AND group, check for badly-placed '|'
                if (*buf == '|') {
                    // found | in an AND environment
                    fprintf(stderr, BNF_ERR "found '|' after tokens in an AND "
                            "grouping, if the two are to be interleaved, group "
                            "with parenthesis the ORs and ANDs separately\n");
                    return and_or_mix;
                }
            }
        }
        else if (!determined_rule_grouping) {
            // by default, go with AND, even if there is only one token
            rule->join_type = (*buf == '|') ? PATTERN_MATCH_OR
                : PATTERN_MATCH_AND;
        }
        // insert node into list of nodes in rule
        plist_node *node = (plist_node*) malloc(sizeof(plist_node));
        rule->last->next = node;
        rule->last = node;
        node->next = NULL;
        node->token = token;

    } while (*buf != term_on);

    return 0;
}



/*
 *
 * file is open FILE* for cnf file
 * linen is the line number we are on in the file (for error reporting)
 *
 * 0 is returned if a rule was parsed, otherwise an error code
 */
static int rule_parse(parse_state *state) {
    rule_lines lines;
    size_t line_size = 0;
    int ret;

    __builtin_memset(lines.lines, 0, sizeof(lines));
    lines.n_lines = 0;

    char *buf, *name;

    // skip empty lines
    do {
        ret = getline(&lines.lines[0], &line_size, state->file);
        state->linen++;
        if (ret == -1) {
            // EOF
            free(lines.lines[0]);
            return eof;
        }
        buf = skip_whitespace(lines.lines[0]);
    } while (*buf == '\0');

    lines.n_lines++;

    // buf is pointing to the beginning of the first rule
    
    if (*buf == '=') {
        // = appeared before a name
        fprintf(stderr, BNF_ERR "rule does not have a name\n");
        return rule_without_name;
    }
    name = buf;
    buf = skip_alpha(name);

    if (*buf == '=') {
        *buf = '\0';
    }
    else if (*buf == '\0') {
        fprintf(stderr, BNF_ERR "rule %s not proceeded by an \"=\"\n", buf);
        return rule_without_eq;
    }
    else {
        *buf = '\0';
        buf++;
        buf = skip_whitespace(buf);
        if (*buf != '=') {
            fprintf(stderr, BNF_ERR "rule %s not proceeded by an \"=\"\n", buf);
            return rule_without_eq;
        }
        buf++;
        buf = skip_whitespace(buf);
    }

    // buf is now at the start of the tokens
    c_pattern *rule = (c_pattern*) calloc(1, sizeof(c_pattern));

    char* hash_name = (char*) malloc(strlen(name) + 1);
    strcpy(hash_name, name);
    // if no rules have been put in the state yet, then declare this rule as
    // the main rule (i.e. the first rule in the file)
    if (state->main_rule == NULL) {
        // since main_rule is a pattern_t (in case in consolidation it is
        // replaced by something simpler), we have to malloc it
        pattern_t *main_rule = (pattern_t*) malloc(sizeof(pattern_t));
        main_rule->patt = rule;
        main_rule->type = TYPE_PATTERN;
        state->main_rule = main_rule;
    }
    hash_insert(&state->rules, hash_name, rule);

    return token_group_parse(state, &lines, rule, &buf, '\n');
}


c_pattern* bnf_parse(const char *bnf_path) {
    parse_state state = {
        .file = fopen(bnf_path, "r"),
        .linen = 0,
        .main_rule = NULL
    };

    if (state.file == NULL) {
        dprintf(STDERR_FILENO, "Unable to open file %s\n", bnf_path);
        return NULL;
    }

    if (hash_init(&state.rules, &str_hash, &str_cmp) != 0) {
        dprintf(STDERR_FILENO, "Unable to initialize hashmap\n");
        return NULL;
    }
    init_parsers();

    

    hash_free(&state.rules);
    fclose(state.file);
    return NULL;
}


void bnf_free(c_pattern *patt) {
    for (plist_node *n = patt->first; n != NULL; n = n->next) {
        struct token *t = n->token;
        if (token_type(t) == TYPE_PATTERN) {
            // if this is a pattern, then we need to recursively follow it down
            // to free all of its children
            bnf_free(t->node.patt);
        }
        else {
            // if this is not a pattern, then it was a single, individually
            // allocated object, which can be freed from any of the pointer
            // types in the union
            free(t->node.cc);
        }
    }
    free(patt);
}

