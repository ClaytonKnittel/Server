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
                .node = (pattern_t*) &num,
                .next = NULL,
                .min = 4,
                .max = 4,
                .flags = 0
            },
            dash2 = {
                .node = (pattern_t*) &dash,
                .next = &dig3,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            dig2 = {
                .node = (pattern_t*) &num,
                .next = &dash2,
                .min = 3,
                .max = 3,
                .flags = 0
            },
            dash1 = {
                .node = (pattern_t*) &dash,
                .next = &dig2,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            dig1 = {
                .node = (pattern_t*) &num,
                .next = &dash1,
                .min = 3,
                .max = 3,
                .flags = 0
            };

        c_pattern patt = {
            .type = TYPE_PATTERN,
            .join_type = PATTERN_MATCH_AND,
            .first = &dig1,
            .last = &dig3
        };


        assert(pattern_match((pattern_t*) &patt, "314-159-2653", 0, NULL),
                0);
        assert(pattern_match((pattern_t*) &patt, "314.159-2653", 0, NULL),
                MATCH_FAIL);
        assert(pattern_match((pattern_t*) &patt, "314-159-265", 0, NULL),
                MATCH_FAIL);
        assert(pattern_match((pattern_t*) &patt, "314-159-26533", 0, NULL),
                MATCH_FAIL);
        assert(pattern_match((pattern_t*) &patt, "314-1f9-2653", 0, NULL),
                MATCH_FAIL);
        assert(pattern_match((pattern_t*) &patt, "3141243233", 0, NULL),
                MATCH_FAIL);
        assert(pattern_match((pattern_t*) &patt, "314-15-32653", 0, NULL),
                MATCH_FAIL);


        // test capture groups on phone numbers
        dig1.flags |= TOKEN_CAPTURE;
        dig2.flags |= TOKEN_CAPTURE;

        match_t matches[2];

        assert(pattern_match((pattern_t*) &patt, "314-159-2653", 2, matches),
                0);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 4);
        assert(matches[1].eo, 7);

        memset(matches, 0, sizeof(matches));
        assert(pattern_match((pattern_t*) &patt, "314-159-2653", 1, matches),
                MATCH_OVERFLOW);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 0);
        assert(matches[1].eo, 0);

        dig1.flags &= ~TOKEN_CAPTURE;
        dig2.flags &= ~TOKEN_CAPTURE;
        dig3.flags |= TOKEN_CAPTURE;

        assert(pattern_match((pattern_t*) &patt, "314-159-2653", 2, matches),
                0);
        assert(matches[0].so, 8);
        assert(matches[0].eo, 12);
        assert(matches[1].so, -1);

    }

    {
        // match emails
        char_class unres, at;
        struct test {
            int type;
            unsigned length;
            char dom[10];
        }
        wu = {
            .type = TYPE_LITERAL,
            .length = sizeof("wustl.edu") - 1,
            .dom = "wustl.edu"
        },
        um = {
            .type = TYPE_LITERAL,
            .length = sizeof("umich.edu") - 1,
            .dom = "umich.edu"
        };

        cc_clear(&unres);
        cc_clear(&at);

        cc_allow_all(&unres);
        cc_disallow(&unres, '@');

        cc_allow(&at, '@');

        struct token
            wut = {
                .node = (pattern_t*) &wu,
                .next = NULL,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            umt = {
                .node = (pattern_t*) &um,
                .next = &wut,
                .min = 1,
                .max = 1,
                .flags = 0
            };

        c_pattern dom_pat = {
            .type = TYPE_PATTERN,
            .join_type = PATTERN_MATCH_OR,
            .first = &umt,
            .last = &wut
        };

        struct token
            dom_patt = {
                .node = (pattern_t*) &dom_pat,
                .next = NULL,
                .min = 1,
                .max = 1,
                .flags = TOKEN_CAPTURE
            },
            att = {
                .node = (pattern_t*) &at,
                .next = &dom_patt,
                .min = 1,
                .max = 1,
                .flags = 0
            },
            user = {
                .node = (pattern_t*) &unres,
                .next = &att,
                .min = 1,
                .max = -1,
                .flags = 0
            };

        c_pattern patte = {
            .type = TYPE_PATTERN,
            .join_type = PATTERN_MATCH_AND,
            .first = &user,
            .last = &dom_patt
        };

        pattern_t *patt = (pattern_t*) &patte;

        match_t match;

        assert(pattern_match(patt, "c.j.knittel@wustl.edu", 1, &match), 0);
        assert(match.so, 12);
        assert(match.eo, 21);
        assert(pattern_match(patt, "plknit00@umich.edu", 1, &match), 0);
        assert(match.so, 9);
        assert(match.eo, 18);
        assert(pattern_match(patt, "c.j.knittel@wustf.edu", 1, &match),
                MATCH_FAIL);

    }


    // try compiling bnf's
    {
        char bnf1[] =
            "rule1 = \"a\" \"b\" \"c\"";

        pattern_t *ret = bnf_parseb(bnf1, sizeof(bnf1) - 1);
        assert_neq((long) ret, (long) NULL);
        bnf_print(ret);

        assert(pattern_match(ret, "abc", 0, NULL), 0);
        assert(pattern_match(ret, "ac", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "acb", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "bac", 0, NULL), MATCH_FAIL);

        pattern_free(ret);


        char bnf2[] =
            "  abd = \"ca\" | (\"bad\") | \"ad\"";
        
        ret = bnf_parseb(bnf2, sizeof(bnf2) - 1);
        assert_neq((long) ret, (long) NULL);
        bnf_print(ret);

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
        assert(errno, and_or_mix);
        assert((long) ret, (long) NULL);

        char bnf4[] =
            " main_rule = \"test\" no_rule \n"
            " norule = \"whoops\"";
        ret = bnf_parseb(bnf4, sizeof(bnf4) - 1);
        assert(errno, undefined_symbol);
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
        bnf_print(ret);

        assert(pattern_match(ret, "clayton is cool", 0, NULL), 0);
        assert(pattern_match(ret, "claytoniscool", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "clayton cool", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "clayton is coo", 0, NULL), MATCH_FAIL);


        pattern_free(ret);

        char bnf2[] =
            " main = { rule_1 | ~rule2 } \" is\" [ \" \" rule3 ]\n"
            " rule_1 = \"clayton\"\n"
            " ~rule2 = \"paige\"\n"
            " rule3 = \"a baby\"";

        ret = bnf_parseb(bnf2, sizeof(bnf2) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);

        bnf_print(ret);

        match_t match;

        assert(pattern_match(ret, "clayton is", 1, &match), 0);
        assert(match.so, 0);
        assert(match.eo, 7);
        assert(pattern_match(ret, "paige is", 1, &match), 0);
        assert(match.so, 0);
        assert(match.eo, 5);
        assert(pattern_match(ret, "clayton is a baby", 1, &match), 0);
        assert(match.so, 0);
        assert(match.eo, 7);
        assert(pattern_match(ret, "paige is a baby", 1, &match), 0);
        assert(match.so, 0);
        assert(match.eo, 5);

        pattern_free(ret);



        char bnf3[] =
            " hex = \"0x\" 1*16('0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' \n"
            "               | '8' | '9' | 'a' | 'b' | 'c' | 'd' | 'e' | 'f')";

        ret = bnf_parseb(bnf3, sizeof(bnf3) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);

        bnf_print(ret);

        assert(pattern_match(ret, "0x1", 0, NULL), 0);
        assert(pattern_match(ret, "0x3f", 0, NULL), 0);
        assert(pattern_match(ret, "0xffff3c4b", 0, NULL), 0);
        assert(pattern_match(ret, "0x1122334455667788", 0, NULL), 0);
        assert(pattern_match(ret, "0x11223344556677889", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "0x", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "0x1q", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "0x1 f", 0, NULL), MATCH_FAIL);

        pattern_free(ret);


        // test escape sequences

        char bnf4[] =
            " escapes = '\\x21' | '\\x3b' | '\\x5A'";

        ret = bnf_parseb(bnf4, sizeof(bnf4) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);

        bnf_print(ret);

        assert(pattern_match(ret, "!", 0, NULL), 0);
        assert(pattern_match(ret, ";", 0, NULL), 0);
        assert(pattern_match(ret, "Z", 0, NULL), 0);
        assert(pattern_match(ret, "q", 0, NULL), MATCH_FAIL);

        pattern_free(ret);
    }


    // test backtracking
    {
        pattern_t *ret;

        char bnf[] =
            " rule = (\"a\" | \"ab\") 'c'";

        ret = bnf_parseb(bnf, sizeof(bnf) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);

        bnf_print(ret);

        assert(pattern_match(ret, "abc", 0, NULL), 0);

        pattern_free(ret);
    }


    // test grammars

    {
        // URI specification

        pattern_t *ret;

        ret = bnf_parsef("grammars/http_header.bnf");
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);

        bnf_print(ret);

        assert(pattern_match(ret, "", 0, NULL), 0);
        assert(pattern_match(ret, "/", 0, NULL), 0);
        assert(pattern_match(ret, "/test/path", 0, NULL), 0);
        assert(pattern_match(ret, "http://clayton@www.google.com/", 0, NULL), 0);

        pattern_free(ret);
    }


    fprintf(stderr, P_GREEN "All match tests passed" P_RESET "\n");
    return 0;
}

