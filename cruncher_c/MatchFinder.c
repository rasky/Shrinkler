// Copyright 1999-2020 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Find repeated strings in a data block.

*/

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "MatchFinder.h"
#include "SuffixArray.h"

// Simple heap implementation for match buffer

static void heap_push(IntHeap *heap, int value) {
	if (heap->size >= heap->capacity) {
		heap->capacity = heap->capacity * 2 + 1;
		heap->data = realloc(heap->data, heap->capacity * sizeof(int));
	}
	
	int pos = heap->size++;
	heap->data[pos] = value;
	
	// Bubble up
	while (pos > 0) {
		int parent = (pos - 1) / 2;
		if (heap->data[parent] <= heap->data[pos]) break;
		int temp = heap->data[parent];
		heap->data[parent] = heap->data[pos];
		heap->data[pos] = temp;
		pos = parent;
	}
}

static int heap_pop(IntHeap *heap) {
	if (heap->size == 0) return -1;
	
	int result = heap->data[0];
	heap->data[0] = heap->data[--heap->size];
	
	// Bubble down
	int pos = 0;
	while (1) {
		int left = pos * 2 + 1;
		int right = pos * 2 + 2;
		int smallest = pos;
		
		if (left < heap->size && heap->data[left] < heap->data[smallest])
			smallest = left;
		if (right < heap->size && heap->data[right] < heap->data[smallest])
			smallest = right;
		
		if (smallest == pos) break;
		
		int temp = heap->data[pos];
		heap->data[pos] = heap->data[smallest];
		heap->data[smallest] = temp;
		pos = smallest;
	}
	
	return result;
}

static int heap_top(IntHeap *heap) {
	return heap->size > 0 ? heap->data[0] : -1;
}

static int heap_empty(IntHeap *heap) {
	return heap->size == 0;
}

static void heap_clear(IntHeap *heap) {
	heap->size = 0;
}

static void heap_free(IntHeap *heap) {
	free(heap->data);
	heap->data = NULL;
	heap->size = 0;
	heap->capacity = 0;
}



static int min(int a, int b) {
	return a < b ? a : b;
}

static int max(int a, int b) {
	return a > b ? a : b;
}

MatchFinder* matchfinder_new(unsigned char *data, int length, int min_length, int match_patience, int max_same_length) {
	MatchFinder *finder = malloc(sizeof(MatchFinder));
	if (!finder) return NULL;
	
	finder->data = data;
	finder->length = length;
	finder->min_length = min_length;
	finder->match_patience = match_patience;
	finder->max_same_length = max_same_length;
	
	// Allocate arrays
	finder->suffix_array = malloc((length + 1) * sizeof(int));
	finder->rev_suffix_array = malloc((length + 1) * sizeof(int));
	finder->longest_common_prefix = malloc((length + 1) * sizeof(int));
	
	if (!finder->suffix_array || !finder->rev_suffix_array || !finder->longest_common_prefix) {
		free(finder->suffix_array);
		free(finder->rev_suffix_array);
		free(finder->longest_common_prefix);
		free(finder);
		return NULL;
	}
	
	// Initialize match buffer
	finder->match_buffer.data = NULL;
	finder->match_buffer.size = 0;
	finder->match_buffer.capacity = 0;
	
	// Compute suffix array
	for (int i = 0; i < length; i++) {
		finder->rev_suffix_array[i] = data[i] + 1;
	}
	finder->rev_suffix_array[length] = 0;
	
	computeSuffixArray(finder->rev_suffix_array, finder->suffix_array, length + 1, 257);
	
	// Compute reverse suffix array
	for (int i = 0; i <= length; i++) {
		finder->rev_suffix_array[finder->suffix_array[i]] = i;
	}
	
	// Compute LCP array
	finder->longest_common_prefix[0] = 0;
	finder->longest_common_prefix[length] = 0;
	int h = 0;
	for (int i = 0; i < length; i++) {
		int r = finder->rev_suffix_array[i];
		if (r < length) {
			int j = finder->suffix_array[r + 1];
			int m = length - max(i, j);
			while (h < m && data[i + h] == data[j + h]) {
				h = h + 1;
			}
			finder->longest_common_prefix[r] = h;
			if (h > 0) h = h - 1;
		}
	}
	
	return finder;
}

void matchfinder_free(MatchFinder *finder) {
	if (finder) {
		free(finder->suffix_array);
		free(finder->rev_suffix_array);
		free(finder->longest_common_prefix);
		heap_free(&finder->match_buffer);
		free(finder);
	}
}

void matchfinder_reset(MatchFinder *finder) {
	heap_clear(&finder->match_buffer);
}

static void extend_left(MatchFinder *finder) {
	int iter = 0;
	while (finder->left_length >= finder->min_length) {
		finder->left_length = min(finder->left_length, finder->longest_common_prefix[--finder->left_index]);
		int pos = finder->suffix_array[finder->left_index];
		if (pos < finder->current_pos && pos >= finder->min_pos) break;
		if (++iter > finder->match_patience) {
			finder->left_length = 0;
			break;
		}
	}
}

static void extend_right(MatchFinder *finder) {
	int iter = 0;
	while (1) {
		finder->right_length = min(finder->right_length, finder->longest_common_prefix[finder->right_index]);
		if (finder->right_length < finder->min_length) break;
		int pos = finder->suffix_array[++finder->right_index];
		if (pos < finder->current_pos && pos >= finder->min_pos) break;
		if (++iter > finder->match_patience) {
			finder->right_length = 0;
			break;
		}
	}
}

static int next_length(MatchFinder *finder) {
	return max(finder->left_length, finder->right_length);
}

void matchfinder_begin_matching(MatchFinder *finder, int pos) {
	finder->current_pos = pos;
	finder->min_pos = 0;

	finder->left_index = finder->rev_suffix_array[pos];
	finder->left_length = finder->length - pos;
	extend_left(finder);
	finder->right_index = finder->rev_suffix_array[pos];
	finder->right_length = finder->length - pos;
	extend_right(finder);
}

int matchfinder_next_match(MatchFinder *finder, int *match_pos_out, int *match_length_out) {
	if (heap_empty(&finder->match_buffer)) {
		// Fill match buffer
		finder->current_length = next_length(finder);
		if (finder->current_length < finder->min_length) return 0;
		int new_min_pos = finder->min_pos;
		do {
			int match_pos;
			if (finder->left_length > finder->right_length) {
				match_pos = finder->suffix_array[finder->left_index];
				extend_left(finder);
			} else {
				match_pos = finder->suffix_array[finder->right_index];
				extend_right(finder);
			}
			new_min_pos = max(new_min_pos, match_pos);
			if (finder->match_buffer.size < finder->max_same_length) {
				heap_push(&finder->match_buffer, match_pos);
			} else {
				if (match_pos > heap_top(&finder->match_buffer)) {
					heap_pop(&finder->match_buffer);
					heap_push(&finder->match_buffer, match_pos);
				}
				finder->min_pos = heap_top(&finder->match_buffer);
			}
		} while (next_length(finder) == finder->current_length);
		assert(!heap_empty(&finder->match_buffer));
		finder->min_pos = new_min_pos;
	}

	*match_length_out = finder->current_length;
	*match_pos_out = heap_pop(&finder->match_buffer);
	assert(*match_pos_out < finder->current_pos);
	return 1;
}
