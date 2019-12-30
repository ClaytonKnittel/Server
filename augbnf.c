#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "augbnf.h"
#include "hashmap.h"



#define PATTERN_T 0x0
#define CHAR_CLASS_T 0x1
#define LITERAL_T 0x2
#define RULE_REF_T 0x3



typedef struct rule {
    char *name;
} rule_t;


typedef struct {
    int join_type;
} dep_node;


unsigned rule_hash(void* rule) {
    unsigned long ptr = (unsigned long) rule;
    unsigned lower = ptr & 0xffffffff;
    unsigned upper = ((ptr >> 32) * 0xffffffff);
    return lower ^ upper;
}

int rule_cmp(void* rule1, void* rule2) {
    return (rule1 == rule2) ? 0 : 1;
}



c_pattern* bnf_parse(const char *bnf_path) {
    hashmap rules;
    int fd = open(bnf_path, O_RDONLY);

    if (fd == -1) {
        dprintf(STDERR_FILENO, "Unable to open file %s\n", bnf_path);
        return NULL;
    }

    if (hash_init(&rules, &rule_hash, &rule_cmp) != 0) {
        dprintf(STDERR_FILENO, "Unable to initialize hashmap\n");
        return NULL;
    }

    

    hash_free(&rules);
    close(fd);
    return NULL;
}


void bnf_free(c_pattern *patt) {
    for (int i = 0; i < patt->token_count; i++) {
        struct token *t = patt->token[i];
        if (token_type(t) == TOKEN_TYPE_PATTERN) {
            // if this is a pattern, then we need to recursively follow it down
            // to free all of its children
            bnf_free(t->patt);
        }
        else {
            // if this is not a pattern, then it was a single, individually
            // allocated object, which can be freed from any of the pointer
            // types in the union
            free(t->cc);
        }
    }
    free(patt);
}

