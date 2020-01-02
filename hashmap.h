/*
 * Implementation of a generic hash map, which uses a singly-linked list to
 * hold the contents of each bin
 *
 */

// returned by a method when an element can't be found
#define HASH_ELEMENT_NOT_FOUND 1
#define HASH_ELEMENT_EXISTS 2
#define HASH_MEM_ERR 3

#define LOAD_FACTOR 0.75


// table size source: https://planetmath.org/goodhashtableprimes

static const unsigned int sizes[] = {
    53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317,
    196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843,
    50331653, 100663319, 201326611, 402653189, 805306457, 1610612741
};


struct hash_node {
    // points to subsequent hash_node in the same bucket
    struct hash_node *next;

    // points to key value of this node
    void *k;
    // points to data held by this node
    void *v;

    // to avoid recalculation of hash value when expanding the size of
    // the map
    unsigned hash;
};

struct hash_bucket {
    // points to the first element in the bucket
    struct hash_node *first;
};

typedef struct {
    struct hash_bucket *buckets;

    // index of size in list of primes which gives the number of buckets
    // allocated in the hashmap
    int size_idx;

    // number of elements in the hashmap
    size_t size;
    
    // max number of elements before we need to expand the table
    // (ceil(size * LOAD_FACTOR))
    size_t load_limit;

    // hash function used for hashing
    unsigned (*hash_fn) (void*);
    // comparison function used to compare two keys. Should give 0 if the two
    // elements are equal, and any nonzero value if they differ
    int (*cmp_fn) (void*, void*);
} hashmap;


// generic string hash function
unsigned str_hash(void* v_str);

// generic string cmp function
int str_cmp(void* v_str1, void* v_str2);


// generic pointer hash function
unsigned ptr_hash(void* ptr);

// generic pointer cmp function
int ptr_cmp(void* ptr1, void* ptr2);


struct hash_node* find_next(hashmap *map, struct hash_node* prev,
        size_t *bucket_idx);



struct hash_iterator {
    hashmap *map;
    size_t bucket_idx;
    struct hash_node *node;
};


#define hashmap_for_each(hash_map, key, val) \
    for (   struct hash_iterator __hash_it = { \
                .map = (hash_map), \
                .bucket_idx = 0, \
                .node = find_next(__hash_it.map, NULL, &__hash_it.bucket_idx) \
            }; \
            __hash_it.node != NULL && \
            (((key) = __hash_it.node->k) == __hash_it.node->k) && \
            (((val) = __hash_it.node->v) == __hash_it.node->v); \
            __hash_it.node = find_next(__hash_it.map, __hash_it.node, \
                &__hash_it.bucket_idx))



/*
 * initializes a hashmap, returning 0 on success and -1 on failure
 */
int hash_init(hashmap *map, unsigned (*hash_fn) (void*),
        int (*cmp_fn) (void*, void*));


/*
 * deallocates the hashmap, without freeing any data held in the map
 */
void hash_free(hashmap *map);


/*
 * inserts the given key-value pair into the hashmap using the provided
 * hash_fn from the hashmap constructor. This method checks for duplicate
 * entries and returns an error if one is found, otherwise it returns 0
 */
int hash_insert(hashmap *map, void* k, void* v);

/*
 * changes the given key to instead point to v, discarding whatever it
 * used to point to
 */
int hash_remap(hashmap *map, void* k, void* v);

/*
 * removes the given element from the hashmap
 *
 * returns 0 on success, HASH_ELEMENT_NOT_FOUND if not found
 */
int hash_delete(hashmap *map, void* k);


/*
 * returns the value associated with the given key in the hashtable, or
 * NULL if not found
 */
void* hash_get(hashmap *map, void* k);


/*
 * prints the hashtable, for debugging purposes
 */
void hash_print(hashmap *map);

