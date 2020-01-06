#include <stdlib.h>
#include <string.h>

#include "t_assert.h"

#include "../src/hashmap.h"
#include "../src/vprint.h"


void hash_print(hashmap *map) {
    for (size_t i = 0; i < sizes[map->size_idx]; i++) {
        struct hash_node *n = map->buckets[i].first;
        if (n != NULL) {
            printf("[%7lu]\t", i);
            while (n != NULL) {
                printf("\"%s\" -> %lu\t", (char*) n->k, *(size_t*) n->v);
                n = n->next;
            }
            printf("\n");
        }
    }
}

void hash_print_cond(hashmap *map) {
#define WID 1
#define WIDSTR "1"
#define ROWLEN 76
#define N_PER_ROW ((ROWLEN + 1) / (WID + 3))

    size_t num = 0;
    for (size_t i = 0; i < sizes[map->size_idx]; i++) {
        size_t count = 0;
        struct hash_node *n = map->buckets[i].first;
        while (n != NULL) {
            count++;
            n = n->next;
        }
        if (count == 0) {
            printf("[ ] ");
        }
        else {
            printf("[%" WIDSTR "." WIDSTR "lu] ", count);
        }
        if (++num == N_PER_ROW) {
            num = 0;
            printf("\n");
        }
    }
    if (num != 0) {
        printf("\n");
    }

#undef WID
#undef WIDSTR
#undef ROWLEN
#undef N_PER_ROW
}



int main() {

    hashmap map;

    assert(str_hash_init(&map), 0);

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

#define SIZE (3 * 769 / 4 + 1)
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
    v_ensure(hash_print_cond(&map));

    for (size_t i = 0; i < SIZE; i++) {
        free(ptrs[i]);
    }
    hash_free(&map);


    printf(P_GREEN "All hashmap tests passed" P_RESET "\n");
    return 0;
}

