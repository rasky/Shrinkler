#include <stdlib.h>
#include <assert.h>
#include "Heap.h"

Heap* heap_new(int capacity) {
    Heap *heap = malloc(sizeof(Heap));
    if (!heap) return NULL;
    
    heap->data = malloc(capacity * sizeof(RefEdge*));
    if (!heap->data) {
        free(heap);
        return NULL;
    }
    
    heap->size = 0;
    heap->capacity = capacity;
    
    return heap;
}

void heap_free(Heap *heap) {
    if (heap) {
        free(heap->data);
        free(heap);
    }
}

static void heap_swap(Heap *heap, int i, int j) {
    RefEdge *temp = heap->data[i];
    heap->data[i] = heap->data[j];
    heap->data[j] = temp;
    
    // Update heap indices
    heap->data[i]->_heap_index = i;
    heap->data[j]->_heap_index = j;
}

static void heap_sift_up(Heap *heap, int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (refedge_less(heap->data[parent], heap->data[index])) {
            break;
        }
        heap_swap(heap, index, parent);
        index = parent;
    }
}

static void heap_sift_down(Heap *heap, int index) {
    while (1) {
        int left = 2 * index + 1;
        int right = 2 * index + 2;
        int smallest = index;
        
        if (left < heap->size && refedge_less(heap->data[left], heap->data[smallest])) {
            smallest = left;
        }
        if (right < heap->size && refedge_less(heap->data[right], heap->data[smallest])) {
            smallest = right;
        }
        
        if (smallest == index) {
            break;
        }
        
        heap_swap(heap, index, smallest);
        index = smallest;
    }
}

void heap_insert(Heap *heap, RefEdge *edge) {
    assert(heap->size < heap->capacity);
    
    heap->data[heap->size] = edge;
    edge->_heap_index = heap->size;
    heap->size++;
    
    heap_sift_up(heap, heap->size - 1);
}

RefEdge* heap_remove_largest(Heap *heap) {
    if (heap->size == 0) return NULL;
    
    // Remove the root element (index 0) - O(log n) operation
    return heap_remove(heap, heap->data[0]);
}

RefEdge* heap_remove(Heap *heap, RefEdge *edge) {
    if (heap->size == 0) return NULL;
    
    int index = edge->_heap_index;
    if (index >= heap->size || heap->data[index] != edge) {
        return NULL; // Edge not found
    }
    
    RefEdge *removed = heap->data[index];
    
    // Move last element to this position
    heap->size--;
    if (index < heap->size) {
        heap->data[index] = heap->data[heap->size];
        heap->data[index]->_heap_index = index;
        heap_sift_down(heap, index);
    }
    
    removed->_heap_index = -1; // Mark as removed
    return removed;
}

int heap_contains(Heap *heap, RefEdge *edge) {
    if (edge->_heap_index < 0 || edge->_heap_index >= heap->size) {
        return 0;
    }
    return heap->data[edge->_heap_index] == edge;
}

int heap_empty(Heap *heap) {
    return heap->size == 0;
}

void heap_clear(Heap *heap) {
    heap->size = 0;
}
