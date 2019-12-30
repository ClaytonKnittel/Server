#include <stdlib.h>

#include "../t_assert.h"
#include "../match.h"
#include "../vprint.h"


int main() {
    char_class m;

    cc_clear(&m);

    // test cleared cc
    for (char c = 0; c >= 0; c++) {
        assert(cc_is_match(&m, c), 0);
    }

    cc_allow_lower(&m);
    for (char c = 0; c >= 0; c++) {
        assert(cc_is_match(&m, c), c >= 'a' && c <= 'z');
    }
    cc_allow_upper(&m);
    for (char c = 0; c >= 0; c++) {
        assert(cc_is_match(&m, c), (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z'));
    }

    {
        // match phone numbers
        char_class num, dash;
        cc_clear(&num);
        cc_clear(&dash);

        cc_allow_num(&num);
        cc_allow(&dash, '-');

        struct token
            digs = {
                .cc = &num,
                .min = 3,
                .max = 3,
                .type = TOKEN_TYPE_CC
            },
            digs4 = {
                .cc = &num,
                .min = 4,
                .max = 4,
                .type = TOKEN_TYPE_CC
            },
            dasht = {
                .cc = &dash,
                .min = 1,
                .max = 1,
                .type = TOKEN_TYPE_CC
            };

        c_pattern *patt = (c_pattern*) malloc(sizeof(c_pattern)
                + 5 * sizeof(struct token *));
        patt->join_type = PATTERN_MATCH_AND;
        patt->token_count = 5;
        patt->token[0] = &digs;
        patt->token[1] = &dasht;
        patt->token[2] = &digs;
        patt->token[3] = &dasht;
        patt->token[4] = &digs4;

        assert(pattern_match(patt, "314-159-2653", 0, NULL), 0);
        assert(pattern_match(patt, "314.159-2653", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt, "314-159-265", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt, "314-159-26533", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt, "314-1f9-2653", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt, "3141243233", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt, "314-15-32653", 0, NULL), MATCH_FAIL);
    }

    printf(P_GREEN "All match tests passed" P_RESET "\n");
    return 0;
}

