
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
        assert(cc_is_match(&m, c), (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
    }

    printf(P_GREEN "All match tests passed" P_RESET "\n");
    return 0;
}

