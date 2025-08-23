#ifndef CUCKOO_HASH_H
#define CUCKOO_HASH_H

#include "RefEdge.h"

// Special value to mark unused entries
#define UNUSED_KEY 0x80000000

typedef struct CuckooHashEntry {
    int key;
    RefEdge *value;
} CuckooHashEntry;

typedef struct CuckooHash {
    CuckooHashEntry *data;
    int size;
    int capacity;
    int hash_shift;  // For power-of-2 sizing
} CuckooHash;

// Function declarations
CuckooHash* cuckoohash_new(int capacity);
void cuckoohash_free(CuckooHash *hash);
void cuckoohash_clear(CuckooHash *hash);
void cuckoohash_insert(CuckooHash *hash, int key, RefEdge *value);
RefEdge* cuckoohash_get(CuckooHash *hash, int key);
void cuckoohash_erase(CuckooHash *hash, int key);
int cuckoohash_count(CuckooHash *hash, int key);
int cuckoohash_empty(CuckooHash *hash);

// Iterator-like functionality
typedef struct CuckooHashIterator {
    CuckooHash *hash;
    int index;
} CuckooHashIterator;

CuckooHashIterator cuckoohash_begin(CuckooHash *hash);
int cuckoohash_iterator_valid(CuckooHashIterator *it);
void cuckoohash_iterator_next(CuckooHashIterator *it);
int cuckoohash_iterator_key(CuckooHashIterator *it);
RefEdge* cuckoohash_iterator_value(CuckooHashIterator *it);

#endif // CUCKOO_HASH_H
