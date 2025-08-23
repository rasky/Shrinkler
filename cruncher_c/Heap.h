#ifndef HEAP_H
#define HEAP_H

#include "RefEdge.h"

typedef struct Heap {
    RefEdge **data;
    int size;
    int capacity;
} Heap;

// Function declarations
Heap* heap_new(int capacity);
void heap_free(Heap *heap);
void heap_insert(Heap *heap, RefEdge *edge);
RefEdge* heap_remove_largest(Heap *heap);
RefEdge* heap_remove(Heap *heap, RefEdge *edge);
int heap_contains(Heap *heap, RefEdge *edge);
int heap_empty(Heap *heap);
void heap_clear(Heap *heap);

#endif // HEAP_H
