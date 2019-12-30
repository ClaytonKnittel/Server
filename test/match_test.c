#include <stdlib.h>
#include <string.h>

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


        // test capture groups on phone numbers
        digs.type |= TOKEN_CAPTURE;

        match_t matches[2];

        assert(pattern_match(patt, "314-159-2653", 2, matches), 0);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 4);
        assert(matches[1].eo, 7);

        memset(matches, 0, sizeof(matches));
        assert(pattern_match(patt, "314-159-2653", 1, matches), MATCH_OVERFLOW);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 0);
        assert(matches[1].eo, 0);

        digs.type &= ~TOKEN_CAPTURE;
        digs4.type |= TOKEN_CAPTURE;

        assert(pattern_match(patt, "314-159-2653", 2, matches), 0);
        assert(matches[0].so, 8);
        assert(matches[0].eo, 12);
        assert(matches[1].so, -1);

        free(patt);
    }

    {
        // match emails
        char_class unres, at;
        literal wu, um;
        cc_clear(&unres);
        cc_clear(&at);

        cc_allow_all(&unres);
        cc_disallow(&unres, '@');

        cc_allow(&at, '@');

        wu.lit = "wustl.edu";
        um.lit = "umich.edu";

        struct token
            user = {
                .cc = &unres,
                .min = 1,
                .max = -1,
                .type = TOKEN_TYPE_CC
            },
            att = {
                .cc = &at,
                .min = 1,
                .max = 1,
                .type = TOKEN_TYPE_CC
            },
            wut = {
                .lit = &wu,
                .min = 1,
                .max = 1,
                .type = TOKEN_TYPE_LITERAL
            },
            umt = {
                .lit = &um,
                .min = 1,
                .max = 1,
                .type = TOKEN_TYPE_LITERAL
            };

        c_pattern *dom_pat = (c_pattern*) malloc(sizeof(c_pattern)
                + 2 * sizeof(struct token *));
        dom_pat->join_type = PATTERN_MATCH_OR;
        dom_pat->token_count = 2;
        dom_pat->token[0] = &wut;
        dom_pat->token[1] = &umt;

        struct token dom_patt = {
            .patt = dom_pat,
            .min = 1,
            .max = 1,
            .type = TOKEN_TYPE_PATTERN | TOKEN_CAPTURE
        };

        c_pattern *patt = (c_pattern*) malloc(sizeof(c_pattern)
                + 3 * sizeof(struct token *));
        patt->join_type = PATTERN_MATCH_AND;
        patt->token_count = 3;
        patt->token[0] = &user;
        patt->token[1] = &att;
        patt->token[2] = &dom_patt;

        match_t match;

        assert(pattern_match(patt, "c.j.knittel@wustl.edu", 1, &match), 0);
        assert(match.so, 12);
        assert(match.eo, 21);
        assert(pattern_match(patt, "plknit00@umich.edu", 1, &match), 0);
        assert(match.so, 9);
        assert(match.eo, 18);
        assert(pattern_match(patt, "c.j.knittel@wustf.edu", 1, &match),
                MATCH_FAIL);

        free(patt);
        free(dom_pat);
    }


    printf(P_GREEN "All match tests passed" P_RESET "\n");
    return 0;
}

