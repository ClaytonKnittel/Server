#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../augbnf.h"
#include "../hashmap.h"
#include "../t_assert.h"
#include "../match.h"
#include "../util.h"
#include "../vprint.h"



int _tmp_check(hashmap *seen, token_t *token) {
    if (hash_insert(seen, token, NULL) == 0) {
        // if it wasn't seen before, check its tmp field and recurse on its
        // children
        if (token->tmp != 0) {
            return -1;
        }
        if (patt_type(token->node) == TYPE_TOKEN) {
            if (_tmp_check(seen, &token->node->token) != 0) {
                return -1;
            }
        }
        if (token->next != NULL) {
            if (_tmp_check(seen, token->next) != 0) {
                return -1;
            }
        }
        if (token->alt != NULL) {
            if (_tmp_check(seen, token->alt) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/*
 * checks to make sure this token and all of its descendents' tmp fields are
 * zero
 */
int tmp_check(token_t *token) {
    hashmap seen;
    hash_init(&seen, &ptr_hash, &ptr_cmp);

    int ret = _tmp_check(&seen, token);

    hash_free(&seen);
    return ret;
}


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
        char_class *num, *dash;
        num = (char_class*) make_char_class();
        dash = (char_class*) make_char_class();

        cc_allow_num(num);
        cc_allow(dash, '-');

        struct token
            dig3 = {
                .flags = TYPE_TOKEN,
                .tmp = 0,
                .node = (pattern_t*) num,
                .alt = NULL,
                .next = NULL,
                .min = 4,
                .max = 4,
                .match_idx = 0
            },
            dash2 = {
                .flags = TYPE_TOKEN,
                .tmp = 0,
                .node = (pattern_t*) dash,
                .alt = NULL,
                .next = &dig3,
                .min = 1,
                .max = 1
            },
            dig2 = {
                .flags = TYPE_TOKEN,
                .tmp = 0,
                .node = (pattern_t*) num,
                .alt = NULL,
                .next = &dash2,
                .min = 3,
                .max = 3,
                .match_idx = 1
            },
            dash1 = {
                .flags = TYPE_TOKEN,
                .tmp = 0,
                .node = (pattern_t*) dash,
                .alt = NULL,
                .next = &dig2,
                .min = 1,
                .max = 1
            },
            patt = {
                .flags = TYPE_TOKEN,
                .tmp = 0,
                .node = (pattern_t*) num,
                .alt = NULL,
                .next = &dash1,
                .min = 3,
                .max = 3,
                .match_idx = 0
            };
        patt_ref_inc((pattern_t*) num);
        patt_ref_inc((pattern_t*) dash);

        assert(pattern_match(&patt, "314-159-2653", 0, NULL), 0);
        assert(pattern_match(&patt, "314.159-2653", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "314-159-265", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "314-159-26533", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "314-1f9-2653", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "3141243233", 0, NULL), MATCH_FAIL);
        assert(pattern_match(&patt, "314-15-32653", 0, NULL), MATCH_FAIL);

        // test copy
        token_t *patt2 = pattern_deep_copy(&patt);

        assert(pattern_match(patt2, "314-159-2653", 0, NULL), 0);
        assert(pattern_match(patt2, "314.159-2653", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt2, "314-159-265", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt2, "314-159-26533", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt2, "314-1f9-2653", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt2, "3141243233", 0, NULL), MATCH_FAIL);
        assert(pattern_match(patt2, "314-15-32653", 0, NULL), MATCH_FAIL);

        pattern_free(patt2);


        // test capture groups on phone numbers
        patt.flags |= TOKEN_CAPTURE;
        dig2.flags |= TOKEN_CAPTURE;

        match_t matches[2];
        memset(matches, 0, sizeof(matches));

        assert(pattern_match(&patt, "314-159-2653", 2, matches), 0);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 4);
        assert(matches[1].eo, 7);

        memset(matches, 0, sizeof(matches));
        assert(pattern_match(&patt, "314-159-2653", 1, matches), 0);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 0);
        assert(matches[1].eo, 0);

        patt.flags &= ~TOKEN_CAPTURE;
        dig2.flags &= ~TOKEN_CAPTURE;
        dig3.flags |= TOKEN_CAPTURE;

        memset(matches, 0, sizeof(matches));
        assert(pattern_match(&patt, "314-159-2653", 2, matches), 0);
        assert(matches[0].so, 8);
        assert(matches[0].eo, 12);
        assert(matches[1].so, -1);

        free(num);
        free(dash);

    }

    {
        // match emails
        char_class *unres, *at;

        literal *wu, *um;

        wu = (literal*) make_literal(sizeof("wustl.edu") - 1);
        memcpy(wu->word, "wustl.edu", sizeof("wustl.edu") - 1);
        
        um = (literal*) make_literal(sizeof("umich.edu") - 1);
        memcpy(um->word, "umich.edu", sizeof("umich.edu") - 1);

        unres = (char_class*) make_char_class();
        at = (char_class*) make_char_class();

        cc_allow_all(unres);
        cc_disallow(unres, '@');

        cc_allow(at, '@');

        struct token
            wut = {
                .flags = TYPE_TOKEN | TOKEN_CAPTURE,
                .tmp = 0,
                .node = (pattern_t*) wu,
                .alt = NULL,
                .next = NULL,
                .min = 1,
                .max = 1
            },
            umt = {
                .flags = TYPE_TOKEN | TOKEN_CAPTURE,
                .tmp = 0,
                .node = (pattern_t*) um,
                .alt = &wut,
                .next = NULL,
                .min = 1,
                .max = 1,
            },
            att = {
                .flags = TYPE_TOKEN,
                .tmp = 0,
                .node = (pattern_t*) at,
                .alt = NULL,
                .next = &umt,
                .min = 1,
                .max = 1
            },
            patt = {
                .flags = TYPE_TOKEN,
                .tmp = 0,
                .node = (pattern_t*) unres,
                .alt = NULL,
                .next = &att,
                .min = 1,
                .max = -1
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

        free(wu);
        free(um);
        free(unres);
        free(at);
    }

    // try compiling bnf's
    {
        char bnf1[] =
            "rule1 = \"a\" \"b\" \"c\"";

        token_t *ret = bnf_parseb(bnf1, sizeof(bnf1) - 1);
        assert_neq((long) ret, (long) NULL);
        assert(tmp_check(ret), 0);
        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "abc", 0, NULL), 0);
        assert(pattern_match(ret, "ac", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "acb", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "bac", 0, NULL), MATCH_FAIL);
        assert(tmp_check(ret), 0);

        pattern_free(ret);

        char bnf2[] =
            "  abd = \"ca\" | (\"bad\") | \"ad\"";

        ret = bnf_parseb(bnf2, sizeof(bnf2) - 1);
        assert_neq((long) ret, (long) NULL);
        assert(tmp_check(ret), 0);
        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "ca", 0, NULL), 0);
        assert(pattern_match(ret, "ad", 0, NULL), 0);
        assert(pattern_match(ret, "bad", 0, NULL), 0);
        assert(pattern_match(ret, "cabadad", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "abad", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "ada", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "", 0, NULL), MATCH_FAIL);
        assert(tmp_check(ret), 0);

        pattern_free(ret);

    }

    // test symbol resolution
    {
        token_t *ret;

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
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "clayton is cool", 0, NULL), 0);
        assert(pattern_match(ret, "claytoniscool", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "clayton cool", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "clayton is coo", 0, NULL), MATCH_FAIL);
        assert(tmp_check(ret), 0);


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
        assert(tmp_check(ret), 0);

        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "0x1", 0, NULL), 0);
        assert(pattern_match(ret, "0x3f", 0, NULL), 0);
        assert(pattern_match(ret, "0xffff3c4b", 0, NULL), 0);
        assert(pattern_match(ret, "0x1122334455667788", 0, NULL), 0);
        assert(pattern_match(ret, "0x11223344556677889", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "0x", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "0x1q", 0, NULL), MATCH_FAIL);
        assert(pattern_match(ret, "0x1 f", 0, NULL), MATCH_FAIL);
        assert(tmp_check(ret), 0);

        pattern_free(ret);


        // test escape sequences

        char bnf4[] =
            " escapes = '\\x21' | '\\x3b' | '\\x5A'";

        ret = bnf_parseb(bnf4, sizeof(bnf4) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);
        assert(tmp_check(ret), 0);

        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "!", 0, NULL), 0);
        assert(pattern_match(ret, ";", 0, NULL), 0);
        assert(pattern_match(ret, "Z", 0, NULL), 0);
        assert(pattern_match(ret, "q", 0, NULL), MATCH_FAIL);
        assert(tmp_check(ret), 0);

        pattern_free(ret);
    }



    // test badly formed rexeges
    {
        token_t *ret;
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

        //" main = ( \"ab\" ( \"ef\" (\"cd\" test1 ) ) ) | test2"
        char bnf5[] =
            "main = test1 | test2\n"
            "test1 = \"ab\" test3\n"
            "test2 = \"cd\" test1\n"
            "test3 = \"ef\" test2";
        ret = bnf_parseb(bnf5, sizeof(bnf5) - 1);
        assert(errno, circular_definition);
        assert((long) ret, (long) NULL);
    }


    // test backtracking
    {
        token_t *ret;

        char bnf[] =
            " rule = (\"a\" | \"ab\") 'c'";

        ret = bnf_parseb(bnf, sizeof(bnf) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);
        assert(tmp_check(ret), 0);

        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "abc", 0, NULL), 0);
        assert(tmp_check(ret), 0);

        pattern_free(ret);


        char bnf2[] =
            " rule = 1*3('a' 'c') \"acac\"\n";

        ret = bnf_parseb(bnf2, sizeof(bnf2) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);
        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "acacac", 0, NULL), 0);
        assert(pattern_match(ret, "acacacac", 0, NULL), 0);
        assert(pattern_match(ret, "acacacacac", 0, NULL), 0);
        assert(pattern_match(ret, "acacacacacac", 0, NULL), MATCH_FAIL);

        pattern_free(ret);


        char bnf3[] =
            " rule = '/' path_segments [ '#' *pchar ]\n"
            "path_segments = segment *( '/' segment )\n"
            "segment = *pchar\n"
            "pchar = ( 'a' | 'b' | 'c' | 'd' | 'e' )";

        ret = bnf_parseb(bnf3, sizeof(bnf3) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);
        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "/abc", 0, NULL), 0);
        assert(pattern_match(ret, "/abc/dea", 0, NULL), 0);
        assert(pattern_match(ret, "/dea/", 0, NULL), 0);
        assert(pattern_match(ret, "/", 0, NULL), 0);
        assert(pattern_match(ret, "/abc#abc", 0, NULL), 0);
        assert(pattern_match(ret, "/abc/dea#aad", 0, NULL), 0);
        assert(pattern_match(ret, "/dea/#aaaaaaaaaaaaccccc", 0, NULL), 0);
        assert(pattern_match(ret, "/#", 0, NULL), 0);

        pattern_free(ret);

    }

    // capturing groups again
    {
        token_t *ret;

        char bnf2[] =
            " main = { 1*alpha } ':' [ rule1 ]\n"
            " rule1 = { 2*alpha } \n"
            " alpha = ( 'a' | 'b' | 'c' | 'd' | ; comment !\n"
                        "'e' | 'f' | 'g' )\n";

        ret = bnf_parseb(bnf2, sizeof(bnf2) - 1);
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);
        //bnf_print(ret);

        match_t matches[2];

        assert(pattern_match(ret, "abc:def", 2, &matches[0]), 0);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 3);
        assert(matches[1].so, 4);
        assert(matches[1].eo, 7);
        assert(pattern_match(ret, "a:deg", 2, &matches[0]), 0);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 1);
        assert(matches[1].so, 2);
        assert(matches[1].eo, 5);
        assert(pattern_match(ret, "abcd:defu", 2, &matches[0]), MATCH_FAIL);
        assert(pattern_match(ret, "a:", 2, &matches[0]), 0);
        assert(matches[0].so, 0);
        assert(matches[0].eo, 1);
        assert(matches[1].so, -1);
        
        pattern_free(ret);

    }

    // test reading from file
    {
        token_t *ret;

        ret = bnf_parsef("grammars/test.bnf");
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);
        assert(tmp_check(ret), 0);

        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "hello b", 0, NULL), 0);
        assert(pattern_match(ret, "goodbye c", 0, NULL), 0);
        assert(pattern_match(ret, "hello ab", 0, NULL), MATCH_FAIL);

        pattern_free(ret);

    }

    // test grammars

    {
        // URI specification

        token_t *ret;

        ret = bnf_parsef("grammars/http_header.bnf");
        assert(errno, 0);
        assert_neq((long) ret, (long) NULL);
        assert(tmp_check(ret), 0);

        bnf_print(ret);
        assert(tmp_check(ret), 0);

        assert(pattern_match(ret, "", 0, NULL), 0);
        assert(pattern_match(ret, "/", 0, NULL), 0);
        assert(pattern_match(ret, "/test/path", 0, NULL), 0);
        assert(pattern_match(ret, "http://clayton@www.google.com/",
                    0, NULL), 0);
        assert(pattern_match(ret, "http://www.ics.uci.edu/pub/ietf/uri/#Related",
                    0, NULL), 0);

        // capturing

        match_t matches[6];

        assert(pattern_match(ret,
                    "http://clayton@www.google.com/some/file/a.txt?var=1",
                    6, matches), 0);

        for (int i = 0; i < 6; i++) {
            printf("m%d: (%ld, %ld)\n", i, matches[i].so, matches[i].eo);
        }

        assert(matches[0].so, -1); // fragment
        assert(matches[1].so, 0); // scheme
        assert(matches[1].eo, 4);
        assert(matches[2].so, 29); // abs uri
        assert(matches[2].eo, 45);
        assert(matches[3].so, -1); // rel uri
        assert(matches[4].so, 7); // authority
        assert(matches[4].eo, 29);
        assert(matches[5].so, 46); // query vars
        assert(matches[5].eo, 51);

        //assert(pattern_match(ret, "http://clayton@www.google.com/",
          //          6, matches), 0);
        assert(tmp_check(ret), 0);

        pattern_free(ret);
    }


    fprintf(stderr, P_GREEN "All match tests passed" P_RESET "\n");
    return 0;
}

