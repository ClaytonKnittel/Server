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


// string hash function
unsigned str_hash(void* v_str);

// string cmp function
int str_cmp(void* v_str1, void* v_str2);



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
 * hash_fn from the hashmap constructor. This method does not check for
 * duplicate keys
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

