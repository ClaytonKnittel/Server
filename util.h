#include <stddef.h>

// gives the character width of the decimal representation of
// the number 2 ^ pow2, where '^' is the power operation
int dec_width(int pow2);

// gives the first set bit of val, assuming val is not zero
int first_set_bit(size_t val);

// gives the number of logical cores on this machine
int get_n_cpus();
