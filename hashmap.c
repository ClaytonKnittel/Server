#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hashmap.h"
#include "util.h"
#include "vprint.h"


/*
 * taken from https://github.com/ndevilla/iniparser/blob/master/src/dictionary.c
 */
unsigned str_hash(const char* str) {
    unsigned hash = 0;

    while (*str != '\0') {
        hash += (unsigned) *str;
        hash += hash << 10;
        hash ^= hash >> 6;
        str++;
    }
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    return hash;
}

int str_cmp(const char* str1, const char* str2) {
    for (; *str1 != '\0' && *str2 != '\0' && *str1 == *str2; str1++, str2++);
    return (*str1 == *str2) ? 0 : 1;
}


/*
 * taken from http://web.archive.org/web/20071223173210/http://
 * www.concentric.net/~Ttwang/tech/inthash.htm
 */
unsigned ptr_hash(const void* ptr) {
    unsigned long val = (unsigned long) ptr;
    val = (~val) + (val << 18);
    val ^= (val >> 31);
    val *= 21;
    val &= (val >> 11);
    val += val << 6;
    val ^= val >> 22;
    return (unsigned) val;
}

int ptr_cmp(const void* ptr1, const void* ptr2) {
    return (ptr1 == ptr2) ? 0 : 1;
}



struct hash_node* find_next(hashmap *map, struct hash_node* prev,
        size_t *bucket_idx) {
    if (prev != NULL) {
        prev = prev->next;
    }
    else {
        prev = map->buckets[0].first;
    }
    while (prev == NULL) {
        if (++(*bucket_idx) >= sizes[map->size_idx]) {
            break;
        }
        prev = map->buckets[*bucket_idx].first;
    }
    return prev;
}





int hash_init(hashmap *map, unsigned (*hash_fn) (const void*),
        int (*cmp_fn) (const void*, const void*)) {

    size_t malloc_size = sizes[0] * sizeof(struct hash_bucket);
    map->buckets = (struct hash_bucket*) malloc(malloc_size);

    if (map->buckets == NULL) {
        return -1;
    }

    __builtin_memset(map->buckets, 0, malloc_size);

    map->size_idx = 0;
    map->size = 0;
    map->load_limit = ceil(sizes[0] * LOAD_FACTOR);
    map->hash_fn = hash_fn;
    map->cmp_fn = cmp_fn;
    return 0;
}


void hash_free(hashmap *map) {
    for (size_t i = 0; i < sizes[map->size_idx]; i++) {
        for (struct hash_node *n = map->buckets[i].first; n != NULL;) {
            struct hash_node *nex = n->next;
            free(n);
            n = nex;
        }
    }
    free(map->buckets);
}



#define GOLDEN_RATIO 0.61803398875

/*
 * gives the bucket index of the provided hash value for a table of size
 * n_buckets
 */
static size_t _get_idx(unsigned hash, size_t n_buckets) {
    double a = fmod(GOLDEN_RATIO * hash, 1.);
    size_t idx = floor(a * n_buckets);
    return idx;
}


/*
 * inserts a node into the hashtable assuming the hash field of the node
 * has alreay been computed
 *
 * returns 0 on success and -1 on failure
 */
static int _hash_inserter(hashmap *map, struct hash_node *node) {
    size_t idx = _get_idx(node->hash, sizes[map->size_idx]);

    for (struct hash_node *itm = map->buckets[idx].first; itm != NULL;
            itm = itm->next) {

        if (itm->hash == node->hash
                && map->cmp_fn(itm->k, node->k) == 0) {
            // duplicate entry
            return -1;
        }
    }
    // insert node into beginning of bucket
    node->next = map->buckets[idx].first;
    map->buckets[idx].first = node;
    return 0;
}

/*
 * inserts a node into the hashtable assuming the hash field of the node
 * has alreay been computed, and does not check for duplicate entries
 */
static void _hash_inserter_unsafe(hashmap *map, struct hash_node *node) {
    size_t idx = _get_idx(node->hash, sizes[map->size_idx]);

    // insert node into beginning of bucket
    node->next = map->buckets[idx].first;
    map->buckets[idx].first = node;
}


static int _hash_grow(hashmap *map) {
    map->size_idx++;

    struct hash_bucket *oldbs = map->buckets;
    size_t malloc_size = sizes[map->size_idx] * sizeof(struct hash_bucket);
    map->buckets = (struct hash_bucket *) malloc(malloc_size);
    if (map->buckets == NULL) {
        return -1;
    }

    memset(map->buckets, 0, malloc_size);

    for (size_t i = 0; i < sizes[map->size_idx - 1]; i++) {
        for (struct hash_node *n = oldbs[i].first; n != NULL;) {
            struct hash_node *nex = n->next;
            _hash_inserter_unsafe(map, n);
            n = nex;
        }
    }
    free(oldbs);

    map->load_limit = ceil(sizes[map->size_idx] * LOAD_FACTOR);
    return 0;
}


int hash_insert(hashmap *map, void* k, void* v) {
    struct hash_node *node = (struct hash_node *)
        malloc(sizeof(struct hash_node));
    if (node == NULL) {
        return HASH_MEM_ERR;
    }

    node->k = k;
    node->v = v;
    node->hash = map->hash_fn(k);

    if (_hash_inserter(map, node) != 0) {
        free(node);
        return HASH_ELEMENT_EXISTS;
    }
    map->size++;

    if (map->size > map->load_limit) {
        return _hash_grow(map);
    }

    return 0;
}


/*
 * finds the hash_node struct containing the given key, or NULL if the key
 * is not in the map
 */
static struct hash_node *_hash_find(hashmap *map, const void *k) {
    unsigned hash = map->hash_fn(k);
    size_t idx = _get_idx(hash, sizes[map->size_idx]);

    struct hash_node *node;
    for (node = map->buckets[idx].first; node != NULL
            && (node->hash != hash || map->cmp_fn(node->k, k) != 0);
            node = node->next);

    return node;
}


int hash_remap(hashmap *map, const void* k, void* v) {
    struct hash_node *node = _hash_find(map, k);
    if (node == NULL) {
        return HASH_ELEMENT_NOT_FOUND;
    }
    else {
        node->v = v;
        return 0;
    }
}


int hash_delete(hashmap *map, const void* k) {
    unsigned hash = map->hash_fn(k);
    size_t idx = _get_idx(hash, sizes[map->size_idx]);

    map->size--;

    struct hash_node *node, *prev = NULL;
    for (node = map->buckets[idx].first; node != NULL
            && node->hash != hash && map->cmp_fn(node->k, k) != 0;
            node = node->next) {
        prev = node;
    }

    if (node == NULL) {
        return HASH_ELEMENT_NOT_FOUND;
    }

    // unlink from list
    if (prev == NULL) {
        map->buckets[idx].first = node->next;
    }
    else {
        prev->next = node->next;
    }

    free(node);

    return 0;
}


void* hash_get(hashmap *map, const void* k) {
    struct hash_node *node = _hash_find(map, k);
    if (node == NULL) {
        return NULL;
    }
    else {
        return node->v;
    }
}


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
