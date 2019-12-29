#ifndef _MATCH_H
#define _MATCH_H

#include <stdint.h>

// char codes 0-255
#define NUM_CHARS 128

#define __bitv_t_shift 6
#define __bitv_t_size 64
#define __bitv_t uint64_t
#define __bitv_t_mask ((1 << __bitv_t_shift) - 1)


typedef struct {
    unsigned long bitv[NUM_CHARS / (8 * sizeof(unsigned long))];
} char_class;


static __inline void cc_clear(char_class *cc) {
    __builtin_memset(cc, 0, sizeof(char_class));
}

static __inline int cc_is_match(char_class *cc, char c) {
    return (cc->bitv[c >> __bitv_t_shift] & (1LU << (c & __bitv_t_mask))) != 0;
}

static __inline void cc_allow(char_class *cc, char c) {
    cc->bitv[c >> __bitv_t_shift] |= (1LU << (c & __bitv_t_mask));
}

static __inline void cc_disallow(char_class *cc, char c) {
    cc->bitv[c >> __bitv_t_shift] &= ~(1LU << (c & __bitv_t_mask));
}

static __inline void cc_allow_from(char_class *cc, char_class *other) {
    for (int i = 0; i < (sizeof(char_class) / sizeof(__bitv_t)); i++) {
        cc->bitv[i] |= other->bitv[i];
    }
}

static __inline void cc_allow_range(char_class *cc, char l, char h) {
    int start_loc = l >> __bitv_t_shift;
    int end_loc = h >> __bitv_t_shift;
    l &= __bitv_t_mask;
    h &= __bitv_t_mask;

    if (start_loc == end_loc) {
        cc->bitv[start_loc] |= (((1LU << h) << 1) - 1) & ~((1LU << l) - 1);
    }
    else {
        cc->bitv[start_loc] |= ~((1LU << l) - 1);
        while (++start_loc < end_loc) {
            cc->bitv[start_loc] = ~0LU;
        }
        cc->bitv[start_loc] |= ((1LU << h) << 1) - 1;
    }
}

static __inline void cc_allow_lower(char_class *cc) {
    cc_allow_range(cc, 'a', 'z');
}

static __inline void cc_allow_upper(char_class *cc) {
    cc_allow_range(cc, 'A', 'Z');
}

static __inline void cc_allow_alpha(char_class *cc) {
    cc_allow_upper(cc);
    cc_allow_lower(cc);
}

static __inline void cc_allow_num(char_class *cc) {
    cc_allow_range(cc, '0', '9');
}

static __inline void cc_allow_alphanum(char_class *cc) {
    cc_allow_num(cc);
    cc_allow_alpha(cc);
}

#endif /* _MATCH_H */
