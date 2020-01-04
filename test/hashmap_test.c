#include <stdlib.h>
#include <string.h>

#include "../t_assert.h"
#include "../hashmap.h"
#include "../vprint.h"


int main() {

    hashmap map;

    assert(hash_init(&map, &str_hash, &str_cmp), 0);

    char str1[] = "message 1 hey!";
    char str2[] = "message 2 hey!";
    char str3[] = "message 3 hey!";
    char str4[] = "message 4 hey!";
    char str5[] = "not in it";

    size_t data1 = 1;
    size_t data2 = 4;
    size_t data3 = 6;
    size_t data4 = 7;

    assert(hash_insert(&map, str1, &data1), 0);
    assert(hash_insert(&map, str2, &data2), 0);
    assert(hash_insert(&map, str3, &data3), 0);
    assert(hash_insert(&map, str4, &data4), 0);

    assert((long) hash_get(&map, str2), (long) &data2);
    assert((long) hash_get(&map, str1), (long) &data1);
    assert((long) hash_get(&map, str4), (long) &data4);
    assert((long) hash_get(&map, str3), (long) &data3);
    assert((long) hash_get(&map, str5), (long) NULL);

    assert(hash_insert(&map, str1, &data3), HASH_ELEMENT_EXISTS);
    assert(hash_delete(&map, str1), 0);
    assert((long) hash_get(&map, str1), (long) NULL);

#define SIZE 400
    char *bufs[SIZE];
    for (size_t i = 0; i < SIZE; i++) {
        bufs[i] = (char*) malloc(8);
        strcpy(bufs[i], "maaaaoo");
        bufs[i][2] += i / 10;
        bufs[i][3] += i % 10;
        bufs[i][4] += i / 100;
        assert(hash_insert(&map, bufs[i], &bufs[i]), 0);
        assert(map.size, i + 4);
        assert((long) ((double) i / sizes[map.size_idx] <= LOAD_FACTOR), 1);
    }
    v_ensure(hash_print(&map));

    for (size_t i = 0; i < SIZE; i++) {
        free(bufs[i]);
    }

    hash_free(&map);



    assert(hash_init(&map, &ptr_hash, &ptr_cmp), 0);

    void *ptrs[SIZE];
    for (ssize_t i = 0; i < SIZE; i++) {
        ptrs[i] = malloc(i + 8);
        assert(hash_insert(&map, bufs[i], *((void**) bufs[i])), 0);
        assert(map.size, i + 1);
        assert((long) ((double) i / sizes[map.size_idx] <= LOAD_FACTOR), 1);
        for (ssize_t j = (i - 8 < 0 ? 0 : i - 8); j < i; j++) {
            assert(hash_insert(&map, bufs[j], NULL), HASH_ELEMENT_EXISTS);
        }
        assert(map.size, i + 1);
    }
    v_ensure(hash_print(&map));

    for (size_t i = 0; i < SIZE; i++) {
        free(ptrs[i]);
    }
    hash_free(&map);


    printf(P_GREEN "All hashmap tests passed" P_RESET "\n");
    return 0;
}

