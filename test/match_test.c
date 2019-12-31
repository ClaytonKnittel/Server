#include <stdlib.h>
#include <string.h>

#include "../augbnf.h"
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
                .node.cc = &num,
                .node.type = TYPE_CC,
                .min = 3,
                .max = 3,
                .flags = 0
            },
            digs4 = {
                .node.cc = &num,
                .node.type = TYPE_CC,
                .min = 4,
                .max = 4,
                .flags = 0
            },
            dasht = {
                .node.cc = &dash,
                .node.type = TYPE_CC,
                .min = 1,
                .max = 1,
                .flags = 0
            };

        plist_node
            node5 = {
                .next = NULL,
                .token = &digs4
            },
            node4 = {
                .next = &node5,
                .token = &dasht
            },
            node3 = {
                .next = &node4,
                .token = &digs
            },
            node2 = {
                .next = &node3,
                .token = &dasht
            },
            node1 = {
                .next = &node2,
                .token = &digs
            };
        c_pattern pattern = {
            .join_type = PATTERN_MATCH_AND,
            .first = &node1,
            .last = &node5
        };

        pattern_t patt = {
            .patt = &pattern,
            .type = TYPE_PATTERN
        };


        assert(pattern_match(&patt, "314-159-2653", 0, NULL), 0);
        assert(pattern_match(&patt, "314.159-2653", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "314-159-265", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "314-159-26533", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "314-1f9-2653", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "3141243233", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "314-15-32653", 0, NULL), MATCH_FAIL);


        // test capture groups on phone numbers
        digs.flags |= TOKEN_CAPTURE;

        match_t matches[2];

        assert(pattern_match(&patt, "314-159-2653", 2, matches), 0);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 4);
        assert(matches[1].eo, 7);

        memset(matches, 0, sizeof(matches));
        assert(pattern_match(&patt, "314-159-2653", 1, matches), MATCH_OVERFLOW);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 0);
        assert(matches[1].eo, 0);

        digs.flags &= ~TOKEN_CAPTURE;
        digs4.flags |= TOKEN_CAPTURE;

        assert(pattern_match(&patt, "314-159-2653", 2, matches), 0);
        assert(matches[0].so, 8);
        assert(matches[0].eo, 12);
        assert(matches[1].so, -1);

    }

    {
        // match emails
        char_class unres, at;
        char wu[] = "wustl.edu",
             um[] = "umich.edu";
        cc_clear(&unres);
        cc_clear(&at);

        cc_allow_all(&unres);
        cc_disallow(&unres, '@');

        cc_allow(&at, '@');

        struct token
            user = {
                .node.cc = &unres,
                .node.type = TYPE_CC,
                .min = 1,
                .max = -1,
                .flags = 0
            },
            att = {
                .node.cc = &at,
                .node.type = TYPE_CC,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            wut = {
                .node.lit = (literal*) &wu[0],
                .node.type = TYPE_LITERAL,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            umt = {
                .node.lit = (literal*) &um[0],
                .node.type = TYPE_LITERAL,
                .min = 1,
                .max = 1,
                .flags = 0
            };

        plist_node
            node2 = {
                .next = NULL,
                .token = &umt
            },
            node1 = {
                .next = &node2,
                .token = &wut
            };
        c_pattern dom_pat = {
            .join_type = PATTERN_MATCH_OR,
            .first = &node1,
            .last = &node2
        };

        struct token dom_patt = {
            .node.patt = &dom_pat,
            .node.type = TYPE_PATTERN,
            .min = 1,
            .max = 1,
            .flags = TOKEN_CAPTURE
        };

        plist_node
            node5 = {
                .next = NULL,
                .token = &dom_patt
            },
            node4 = {
                .next = &node5,
                .token = &att
            },
            node3 = {
                .next = &node4,
                .token = &user
            };
        c_pattern patte = {
            .join_type = PATTERN_MATCH_AND,
            .first = &node3,
            .last = &node5
        };

        pattern_t patt = {
            .patt = &patte,
            .type = TYPE_PATTERN
        };

        match_t match;

        assert(pattern_match(&patt, "c.j.knittel@wustl.edu", 1, &match), 0);
        assert(match.so, 12);
        assert(match.eo, 21);
        assert(pattern_match(&patt, "plknit00@umich.edu", 1, &match), 0);
        assert(match.so, 9);
        assert(match.eo, 18);
        assert(pattern_match(&patt, "c.j.knittel@wustf.edu", 1, &match),
                MATCH_FAIL);

    }


    // try compiling bnf's
    {
        char bnf1[] =
            "rule1 = \"a\" \"b\" \"c\"";

        pattern_t *ret = bnf_parseb(bnf1, sizeof(bnf1) - 1);
        assert_neq((long) ret, (long) NULL);

        bnf_free(ret);
    }


    printf(P_GREEN "All match tests passed" P_RESET "\n");
    return 0;
}

