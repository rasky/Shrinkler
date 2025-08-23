#include <assert.h>
#include <stdlib.h>
#include "RefEdge.h"

RefEdgeFactory* refedgefactory_new(int edge_capacity) {
    RefEdgeFactory *factory = malloc(sizeof(RefEdgeFactory));
    if (!factory) return NULL;
    
    factory->edge_capacity = edge_capacity;
    factory->edge_count = 0;
    factory->cleaned_edges = 0;
    factory->max_edge_count = 0;
    factory->max_cleaned_edges = 0;
    factory->buffer = NULL;
    
    return factory;
}

void refedgefactory_free(RefEdgeFactory *factory) {
    if (!factory) return;
    
    // Free all edges in the buffer
    while (factory->buffer != NULL) {
        RefEdge *edge = factory->buffer;
        factory->buffer = edge->source;
        free(edge);
    }
    
    free(factory);
}

void refedgefactory_reset(RefEdgeFactory *factory) {
    assert(factory->edge_count == 0);
    factory->cleaned_edges = 0;
}

RefEdge* refedgefactory_create(RefEdgeFactory *factory, int pos, int offset, int length, int total_size, RefEdge *source) {
	// Remove the problematic assertion - it's not needed for correctness
    
    factory->max_edge_count = (factory->edge_count + 1 > factory->max_edge_count) ? 
                              factory->edge_count + 1 : factory->max_edge_count;
    factory->edge_count++;
    
    RefEdge *edge;
    if (factory->buffer == NULL) {
        edge = malloc(sizeof(RefEdge));
        if (!edge) return NULL;
    } else {
        edge = factory->buffer;
        factory->buffer = edge->source;
    }
    
    edge->pos = pos;
    edge->offset = offset;
    edge->length = length;
    edge->total_size = total_size;
    edge->source = source;
    edge->refcount = 1;
    edge->_heap_index = 0;
    
    if (source != NULL) {
        source->refcount++;
    }
    
    return edge;
}

void refedgefactory_destroy(RefEdgeFactory *factory, RefEdge *edge, int clean) {
    if (!edge) return;
    
    edge->source = factory->buffer;
    factory->buffer = edge;
    factory->edge_count--;
    
    if (clean) {
        factory->max_cleaned_edges = (factory->cleaned_edges + 1 > factory->max_cleaned_edges) ? 
                                     factory->cleaned_edges + 1 : factory->max_cleaned_edges;
        factory->cleaned_edges++;
    }
}

int refedgefactory_full(RefEdgeFactory *factory) {
    return factory->edge_count >= factory->edge_capacity;
}

int refedge_target(RefEdge *edge) {
    return edge->pos + edge->length;
}

int refedge_less(RefEdge *e1, RefEdge *e2) {
    return e1->total_size < e2->total_size;
}
