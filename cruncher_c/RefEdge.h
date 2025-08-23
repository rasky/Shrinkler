#ifndef REFEDGE_H
#define REFEDGE_H

#include <stdlib.h>

// For each offset: Best total size with last ref having that offset
typedef struct RefEdge {
    int pos;
    int offset;
    int length;
    int total_size;
    int refcount;
    struct RefEdge *source;
    int _heap_index;
} RefEdge;

// Factory for RefEdge objects which recycles destroyed objects for efficiency
typedef struct RefEdgeFactory {
    int edge_capacity;
    int edge_count;
    int cleaned_edges;
    int max_edge_count;
    int max_cleaned_edges;
    RefEdge *buffer;
} RefEdgeFactory;

// Function declarations
RefEdgeFactory* refedgefactory_new(int edge_capacity);
void refedgefactory_free(RefEdgeFactory *factory);
void refedgefactory_reset(RefEdgeFactory *factory);
RefEdge* refedgefactory_create(RefEdgeFactory *factory, int pos, int offset, int length, int total_size, RefEdge *source);
void refedgefactory_destroy(RefEdgeFactory *factory, RefEdge *edge, int clean);
int refedgefactory_full(RefEdgeFactory *factory);

// Helper functions
int refedge_target(RefEdge *edge);
int refedge_less(RefEdge *e1, RefEdge *e2);

#endif // REFEDGE_H
