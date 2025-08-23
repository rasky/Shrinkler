// Copyright 1999-2015 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Abstract interface for entropy coding.

*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "Coder.h"

Coder* coder_new(void) {
	Coder *coder = malloc(sizeof(Coder));
	if (!coder) return NULL;
	
	coder->cacheable = 0;
	coder->has_cache = 0;
	coder->number_context_offset = 0;
	coder->n_number_contexts = 0;
	coder->cache = NULL;
	coder->cache_sizes = NULL;
	coder->code = NULL; // Virtual function - must be set by derived class
	
	return coder;
}

void coder_free(Coder *coder) {
	if (coder) {
		if (coder->cache) {
			for (int i = 0; i < coder->n_number_contexts; i++) {
				free(coder->cache[i]);
			}
			free(coder->cache);
		}
		free(coder->cache_sizes);
		free(coder);
	}
}

int coder_code(Coder *coder, int context, int bit) {
	assert(coder && coder->code);
	return coder->code(coder, context, bit);
}

int coder_encode_number(Coder *coder, int base_context, int number) {
	assert(number >= 2);

	if (coder->has_cache) {
		int context_index = (base_context - coder->number_context_offset) >> 8;
		if (context_index >= 0 && context_index < coder->n_number_contexts) {
			unsigned short *cache_for_context = coder->cache[context_index];
			if (number < coder->cache_sizes[context_index]) {
				return cache_for_context[number];
			}
		}
	}

	int size = 0;
	int context;
	int i;
	for (i = 0 ; (4 << i) <= number ; i++) {
		context = base_context + (i * 2 + 2);
		size += coder_code(coder, context, 1);
	}
	context = base_context + (i * 2 + 2);
	size += coder_code(coder, context, 0);

	for (; i >= 0 ; i--) {
		int bit = ((number >> i) & 1);
		context = base_context + (i * 2 + 1);
		size += coder_code(coder, context, bit);
	}

	return size;
}

void coder_set_number_contexts(Coder *coder, int number_context_offset, int n_number_contexts, int max_number) {
	if (!coder->cacheable) return;

	coder->number_context_offset = number_context_offset;
	coder->n_number_contexts = n_number_contexts;
	
	// Free existing cache
	if (coder->cache) {
		for (int i = 0; i < coder->n_number_contexts; i++) {
			free(coder->cache[i]);
		}
		free(coder->cache);
	}
	free(coder->cache_sizes);
	
	// Allocate new cache
	coder->cache = malloc(n_number_contexts * sizeof(unsigned short*));
	coder->cache_sizes = malloc(n_number_contexts * sizeof(int));
	if (!coder->cache || !coder->cache_sizes) {
		free(coder->cache);
		free(coder->cache_sizes);
		coder->cache = NULL;
		coder->cache_sizes = NULL;
		return;
	}
	
	for (int context_index = 0 ; context_index < n_number_contexts ; context_index++) {
		int base_context = number_context_offset + (context_index << 8);
		coder->cache[context_index] = malloc(4 * sizeof(unsigned short));
		coder->cache_sizes[context_index] = 4;
		
		unsigned short *c = coder->cache[context_index];
		c[2] = coder_code(coder, base_context + 2, 0) + coder_code(coder, base_context + 1, 0);
		c[3] = coder_code(coder, base_context + 2, 0) + coder_code(coder, base_context + 1, 1);
		
		int prev_base = 2;
		for (int data_bits = 2 ; data_bits < 30 ; data_bits++) {
			int base = coder->cache_sizes[context_index];
			int base_sizedif = - coder_code(coder, base_context + data_bits * 2 - 2, 0)
			                   + coder_code(coder, base_context + data_bits * 2 - 2, 1)
			                   + coder_code(coder, base_context + data_bits * 2, 0);
			
			// Resize cache if needed
			int new_size = base + (1 << data_bits);
			if (new_size > max_number) new_size = max_number;
			
			unsigned short *new_cache = realloc(coder->cache[context_index], new_size * sizeof(unsigned short));
			if (!new_cache) break;
			coder->cache[context_index] = new_cache;
			c = new_cache;
			
			for (int msb = 0 ; msb <= 1 ; msb++) {
				int sizedif = base_sizedif + coder_code(coder, base_context + data_bits * 2 - 1, msb);
				for (int tail = 0 ; tail < 1 << (data_bits - 1) ; tail++) {
					if (base + tail >= new_size) goto next_context;
					int size = c[prev_base + tail] + sizedif;
					c[base + tail] = size;
				}
			}
			prev_base = base;
			coder->cache_sizes[context_index] = new_size;
		}
		next_context:;
	}

	coder->has_cache = 1;
}
