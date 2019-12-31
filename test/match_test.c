#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../augbnf.h"
#include "../t_assert.h"
#include "../match.h"
#include "../util.h"
#include "../vprint.h"


int main() {
    //silence_stdout();
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
            dig3 = {
                .node.cc = &num,
                .node.type = TYPE_CC,
                .next = NULL,
                .min = 4,
                .max = 4,
                .flags = 0
            },
            dash2 = {
                .node.cc = &dash,
                .node.type = TYPE_CC,
                .next = &dig3,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            dig2 = {
                .node.cc = &num,
                .node.type = TYPE_CC,
                .next = &dash2,
                .min = 3,
                .max = 3,
                .flags = 0
            },
            dash1 = {
                .node.cc = &dash,
                .node.type = TYPE_CC,
                .next = &dig2,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            dig1 = {
                .node.cc = &num,
                .node.type = TYPE_CC,
                .next = &dash1,
                .min = 3,
                .max = 3,
                .flags = 0
            };

        c_pattern pattern = {
            .join_type = PATTERN_MATCH_AND,
            .first = &dig1,
            .last = &dig3
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
        dig1.flags |= TOKEN_CAPTURE;
        dig2.flags |= TOKEN_CAPTURE;

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

        dig1.flags &= ~TOKEN_CAPTURE;
        dig2.flags &= ~TOKEN_CAPTURE;
        dig3.flags |= TOKEN_CAPTURE;

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
            wut = {
                .node.lit = (literal*) &wu[0],
                .node.type = TYPE_LITERAL,
                .next = NULL,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            umt = {
                .node.lit = (literal*) &um[0],
                .node.type = TYPE_LITERAL,
                .next = &wut,
                .min = 1,
                .max = 1,
                .flags = 0
            };

        c_pattern dom_pat = {
            .join_type = PATTERN_MATCH_OR,
            .first = &umt,
            .last = &wut
        };

        struct token
            dom_patt = {
                .node.patt = &dom_pat,
                .node.type = TYPE_PATTERN,
                .next = NULL,
                .min = 1,
                .max = 1,
                .flags = TOKEN_CAPTURE
            },
            att = {
                .node.cc = &at,
                .node.type = TYPE_CC,
                .next = &dom_patt,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            user = {
                .node.cc = &unres,
                .node.type = TYPE_CC,
                .next = &att,
                .min = 1,
                .max = -1,
                .flags = 0
            };

        c_pattern patte = {
            .join_type = PATTERN_MATCH_AND,
            .first = &user,
            .last = &dom_patt
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

        assert(pattern_match(ret, "abc", 0, NULL), 0);
        assert(pattern_match(ret, "ac", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "acb", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "bac", 0, NULL), MATCH_FAIL);

        pattern_free(ret);


        char bnf2[] =
            "  abd = \"ca\" | (\"bad\") | \"ad\"";
        
        ret = bnf_parseb(bnf2, sizeof(bnf2) - 1);
        assert_neq((long) ret, (long) NULL);

        assert(pattern_match(ret, "ca", 0, NULL), 0);
        assert(pattern_match(ret, "ad", 0, NULL), 0);
        assert(pattern_match(ret, "bad", 0, NULL), 0);
        assert(pattern_match(ret, "cabadad", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "abad", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "ada", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "", 0, NULL), MATCH_FAIL);

        pattern_free(ret);

    }

    // test badly formed rexeges
    {
        pattern_t *ret;
        char bnf[] =
            "  abd = \"\"";
        ret = bnf_parseb(bnf, sizeof(bnf) - 1);
        assert(errno, empty_string);
        assert((long) ret, (long) NULL);

        char bnf2[] =
            "  abd = \" fa";
        ret = bnf_parseb(bnf2, sizeof(bnf2) - 1);
        assert(errno, open_string);
        assert((long) ret, (long) NULL);

        char bnf3[] =
            "  abd = \" ab\" ( more |  \n"
            " words  ) | \" help\"";
        ret = bnf_parseb(bnf3, sizeof(bnf3) - 1);
        assert(errno, unexpected_token);
        assert((long) ret, (long) NULL);
    }

    // test symbol resolution
    {
        pattern_t *ret;

        char bnf[] =
            " main = rule1 \" \" rule2 \" \" rule3\n"
            "\n"
            "   rule1 = \"clayton\"\n"
            "   rule2 = \"is\"\n"
            "   rule3 = \"cool\"\n";

        ret = bnf_parseb(bnf, sizeof(bnf) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);

        assert(pattern_match(ret, "clayton is cool", 0, NULL), 0);
        assert(pattern_match(ret, "claytoniscool", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "clayton cool", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "clayton is coo", 0, NULL), MATCH_FAIL);


        pattern_free(ret);
    }


    fprintf(stderr, P_GREEN "All match tests passed" P_RESET "\n");
    return 0;
}

