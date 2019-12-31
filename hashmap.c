#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hashmap.h"
#include "vprint.h"




unsigned str_hash(void* v_str) {
    char* str = (char*) v_str;
    const unsigned p = 53;
    unsigned hash = 0;

    while (*str != '\0') {
        hash = p * hash + *str;
        str++;
    }
    return hash;
}

int str_cmp(void* v_str1, void* v_str2) {
    char* str1 = (char*) v_str1;
    char* str2 = (char*) v_str2;

    for (; *str1 != '\0' && *str2 != '\0' && *str1 == *str2; str1++, str2++);
    return *str1 == *str2;
}


// table size source: https://planetmath.org/goodhashtableprimes

static const unsigned int sizes[] = {
    53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317,
    196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843,
    50331653, 100663319, 201326611, 402653189, 805306457, 1610612741
};


int hash_init(hashmap *map, unsigned (*hash_fn) (void*),
        int (*cmp_fn) (void*, void*)) {

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
 */
static void _hash_inserter(hashmap *map, struct hash_node *node) {
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
            _hash_inserter(map, n);
            n = nex;
        }
    }

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

    _hash_inserter(map, node);
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
static struct hash_node *_hash_find(hashmap *map, void *k) {
    unsigned hash = map->hash_fn(k);
    size_t idx = _get_idx(hash, sizes[map->size_idx]);

    struct hash_node *node;
    for (node = map->buckets[idx].first; node != NULL
            && node->hash != hash && map->cmp_fn(node->k, k) != 0;
            node = node->next);

    return node;
}


int hash_remap(hashmap *map, void* k, void* v) {
    struct hash_node *node = _hash_find(map, k);
    if (node == NULL) {
        return HASH_ELEMENT_NOT_FOUND;
    }
    else {
        node->v = v;
        return 0;
    }
}


int hash_delete(hashmap *map, void* k) {
    unsigned hash = map->hash_fn(k);
    size_t idx = _get_idx(hash, sizes[map->size_idx]);

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


void* hash_get(hashmap *map, void* k) {
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
