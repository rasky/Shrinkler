#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "CuckooHash.h"

// Hash multipliers (same as C++ version)
#define HASH1_MUL 0xF230D3A1
#define HASH2_MUL 0x8084027F
#define INITIAL_SIZE_LOG 2

// Calculate two hash values for a key (same as C++ version)
static void calculate_hashes(int key, int hash_shift, unsigned int *hash1, unsigned int *hash2) {
    unsigned int f = (key << 1) + 1;
    *hash1 = (f * HASH1_MUL) >> hash_shift;
    *hash2 = (f * HASH2_MUL) >> hash_shift;
}

// Get array size based on hash_shift
static int get_array_size(int hash_shift) {
    return 1 << (sizeof(unsigned int) * 8 - hash_shift);
}

// Initialize array with unused entries
static void init_array(CuckooHash *hash) {
    int size = get_array_size(hash->hash_shift);
    hash->data = malloc(size * sizeof(CuckooHashEntry));
    if (!hash->data) return;
    
    for (int i = 0; i < size; i++) {
        hash->data[i].key = UNUSED_KEY;
        hash->data[i].value = NULL;
    }
    hash->capacity = size;
}

// Rehash the table (same as C++ version)
static void rehash(CuckooHash *hash) {
    int old_size = get_array_size(hash->hash_shift);
    CuckooHashEntry *old_data = hash->data;
    
    // Decrease hash_shift to double the size
    hash->hash_shift--;
    hash->size = 0;
    
    // Initialize new array
    init_array(hash);
    
    // Reinsert all valid entries
    for (int i = 0; i < old_size; i++) {
        if (old_data[i].key != UNUSED_KEY) {
            cuckoohash_insert(hash, old_data[i].key, old_data[i].value);
        }
    }
    
    free(old_data);
}

// Insert with cuckoo hashing (same as C++ version)
static void cuckoo_insert(CuckooHash *hash, unsigned int hash_val, int key, RefEdge *value, int max_kicks) {
    CuckooHashEntry *array = hash->data;
    while (array[hash_val].key != UNUSED_KEY) {
        if (--max_kicks < 0) {
            rehash(hash);
            cuckoohash_insert(hash, key, value);
            return;
        }
        
        // Swap current entry with new entry
        int temp_key = array[hash_val].key;
        RefEdge *temp_value = array[hash_val].value;
        array[hash_val].key = key;
        array[hash_val].value = value;
        key = temp_key;
        value = temp_value;
        
        // Calculate new hash for the displaced key
        unsigned int hash1, hash2;
        calculate_hashes(key, hash->hash_shift, &hash1, &hash2);
        hash_val ^= hash1 ^ hash2;
    }
    
    array[hash_val].key = key;
    array[hash_val].value = value;
    hash->size++;
}

CuckooHash* cuckoohash_new(int capacity) {
    CuckooHash *hash = malloc(sizeof(CuckooHash));
    if (!hash) return NULL;
    
    // Optimization: Initialize with a larger size to reduce reallocations
    int initial_size_log = INITIAL_SIZE_LOG;
    if (capacity > 0) {
        // Calculate a more appropriate initial size based on the requested capacity
        while ((1 << initial_size_log) < capacity * 2) {
            initial_size_log++;
        }
    }
    
    hash->hash_shift = sizeof(unsigned int) * 8 - initial_size_log;
    hash->size = 0;
    hash->data = NULL;
    
    init_array(hash);
    if (!hash->data) {
        free(hash);
        return NULL;
    }
    
    return hash;
}

void cuckoohash_free(CuckooHash *hash) {
    if (hash) {
        free(hash->data);
        free(hash);
    }
}

void cuckoohash_clear(CuckooHash *hash) {
    if (hash->data) {
        int size = get_array_size(hash->hash_shift);
        for (int i = 0; i < size; i++) {
            hash->data[i].key = UNUSED_KEY;
            hash->data[i].value = NULL;
        }
    }
    hash->size = 0;
}

void cuckoohash_insert(CuckooHash *hash, int key, RefEdge *value) {
    if (!hash->data) {
        init_array(hash);
    }
    
    unsigned int hash1, hash2;
    calculate_hashes(key, hash->hash_shift, &hash1, &hash2);
    
    CuckooHashEntry *array = hash->data;
    
    // Try first hash location
    if (array[hash1].key == key) {
        array[hash1].value = value;
        return;
    }
    
    // Try second hash location
    if (array[hash2].key == key) {
        array[hash2].value = value;
        return;
    }
    
    // Try to insert at first location
    if (array[hash1].key == UNUSED_KEY) {
        array[hash1].key = key;
        array[hash1].value = value;
        hash->size++;
        return;
    }
    
    // Try to insert at second location
    if (array[hash2].key == UNUSED_KEY) {
        array[hash2].key = key;
        array[hash2].value = value;
        hash->size++;
        return;
    }
    
    // Both locations occupied, need to evict
    cuckoo_insert(hash, hash1, key, value, hash->size);
}

RefEdge* cuckoohash_get(CuckooHash *hash, int key) {
    if (!hash->data || hash->size == 0) return NULL;
    
    unsigned int hash1, hash2;
    calculate_hashes(key, hash->hash_shift, &hash1, &hash2);
    
    CuckooHashEntry *array = hash->data;
    
    if (array[hash1].key == key) return array[hash1].value;
    if (array[hash2].key == key) return array[hash2].value;
    
    return NULL;
}

void cuckoohash_erase(CuckooHash *hash, int key) {
    if (!hash->data) return;
    
    unsigned int hash1, hash2;
    calculate_hashes(key, hash->hash_shift, &hash1, &hash2);
    
    CuckooHashEntry *array = hash->data;
    
    if (array[hash1].key == key) {
        array[hash1].key = UNUSED_KEY;
        array[hash1].value = NULL;
        hash->size--;
        return;
    }
    
    if (array[hash2].key == key) {
        array[hash2].key = UNUSED_KEY;
        array[hash2].value = NULL;
        hash->size--;
        return;
    }
}

int cuckoohash_count(CuckooHash *hash, int key) {
    return cuckoohash_get(hash, key) != NULL ? 1 : 0;
}

int cuckoohash_empty(CuckooHash *hash) {
    return hash->size == 0;
}

CuckooHashIterator cuckoohash_begin(CuckooHash *hash) {
    CuckooHashIterator it = {hash, 0};
    
    if (hash->data) {
        int size = get_array_size(hash->hash_shift);
        // Find first non-unused entry
        while (it.index < size && hash->data[it.index].key == UNUSED_KEY) {
            it.index++;
        }
    }
    
    return it;
}

int cuckoohash_iterator_valid(CuckooHashIterator *it) {
    if (!it->hash->data) return 0;
    int size = get_array_size(it->hash->hash_shift);
    return it->index < size;
}

void cuckoohash_iterator_next(CuckooHashIterator *it) {
    if (!it->hash->data) return;
    
    int size = get_array_size(it->hash->hash_shift);
    it->index++;
    
    // Find next non-unused entry
    while (it->index < size && it->hash->data[it->index].key == UNUSED_KEY) {
        it->index++;
    }
}

int cuckoohash_iterator_key(CuckooHashIterator *it) {
    return it->hash->data[it->index].key;
}

RefEdge* cuckoohash_iterator_value(CuckooHashIterator *it) {
    return it->hash->data[it->index].value;
}
