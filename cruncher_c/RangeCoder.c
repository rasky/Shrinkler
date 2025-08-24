// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

An entropy coder based on range coding.

*/

#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "RangeCoder.h"

static int sizetable[128];
static int sizetable_init = 0;

// Forward declaration
static void set_rangecoder_function(RangeCoder *coder);

static int init_sizetable() {
	for (int i = 0 ; i < 128 ; i++) {
		sizetable[i] = (int) floor(0.5 + (8.0 - log((double) (128 + i)) / log(2.0)) * (1 << BIT_PRECISION));

	}
	return 1;
}

static void add_bit(RangeCoder *coder) {
	int pos = coder->dest_bit;
	int bytepos;
	int bitmask;
	do {
		pos--;
		if (pos < 0) return;
		bytepos = pos >> 3;
		bitmask = 0x80 >> (pos & 7);
		
		while (bytepos >= *coder->out_size) {
			*coder->out = realloc(*coder->out, (*coder->out_size + 1) * sizeof(unsigned char));
			(*coder->out)[*coder->out_size] = 0;
			(*coder->out_size)++;
		}
		(*coder->out)[bytepos] ^= bitmask;
	} while (((*coder->out)[bytepos] & bitmask) == 0);
}

RangeCoder* rangecoder_new(int n_contexts, unsigned char **out, int *out_size) {
	if (!sizetable_init) {
		sizetable_init = init_sizetable();
	}
	
	RangeCoder *coder = malloc(sizeof(RangeCoder));
	if (!coder) return NULL;
	
	coder->contexts = calloc(n_contexts, sizeof(unsigned short));
	if (!coder->contexts) {
		free(coder);
		return NULL;
	}
	
	for (int i = 0; i < n_contexts; i++) {
		coder->contexts[i] = 0x8000;
	}
	
	coder->out = out;
	coder->out_size = out_size;
	coder->dest_bit = -1;
	coder->intervalsize = 0x8000;
	coder->intervalmin = 0;
	coder->trace_file = NULL;  // Initialize trace file to NULL
	
	// Set up virtual function table
	coder->base.cacheable = 0;
	coder->base.has_cache = 0;
	coder->base.cache = NULL;
	coder->base.cache_sizes = NULL;
	
	set_rangecoder_function(coder);
	return coder;
}

void rangecoder_free(RangeCoder *coder) {
	if (coder) {
		free(coder->contexts);
		free(coder);
	}
}

int rangecoder_code(RangeCoder *coder, int context_index, int bit) {
	// Handle special context indices
	if (context_index < 0) {
		// Special contexts like CONTEXT_REPEATED (-1) are handled differently
		// For now, just return 0 size for these
		return 0;
	}
	assert(context_index < 1024); // Assuming max contexts
	assert(bit == 0 || bit == 1);
	
	// Calculate size_before, handling the case where dest_bit is -1 (initial state)
	int size_before;
	if (coder->dest_bit < 0) {
		// Initial state: dest_bit is -1, so size is just the sizetable entry
		size_before = sizetable[(coder->intervalsize - 0x8000) >> 8];
	} else {
		size_before = (coder->dest_bit << BIT_PRECISION) + sizetable[(coder->intervalsize - 0x8000) >> 8];
	}
	
	// Add bounds checking and tracing
	if (context_index < 0 || context_index >= 1024) {
		fprintf(stderr, "ERROR: Invalid context_index %d\n", context_index);
		return 0;
	}
	unsigned prob = coder->contexts[context_index];
	
	unsigned threshold = (coder->intervalsize * prob) >> 16;
	unsigned new_prob;
	
	// Trace the input state
	rangecoder_trace_state(coder, "CODE_START", context_index, bit, size_before);
	
	if (!bit) {
		// Zero
		coder->intervalmin += threshold;
		if (coder->intervalmin & 0x10000) {
			add_bit(coder);
		}
		coder->intervalsize = coder->intervalsize - threshold;
		new_prob = prob - (prob >> 4); // ADJUST_SHIFT = 4
	} else {
		// One
		coder->intervalsize = threshold;
		new_prob = prob + (0xffff >> 4) - (prob >> 4);
	}
	
	assert(new_prob > 0);
	assert(new_prob < 0x10000);
	coder->contexts[context_index] = new_prob;
	
	while (coder->intervalsize < 0x8000) {
		coder->dest_bit++;
		coder->intervalsize <<= 1;
		coder->intervalmin <<= 1;
		if (coder->intervalmin & 0x10000) {
			add_bit(coder);
		}
	}
	coder->intervalmin &= 0xffff;
	
	// Calculate size_after, handling the case where dest_bit is -1 (initial state)
	int size_after;
	if (coder->dest_bit < 0) {
		// Initial state: dest_bit is -1, so size is just the sizetable entry
		size_after = sizetable[(coder->intervalsize - 0x8000) >> 8];
	} else {
		size_after = (coder->dest_bit << BIT_PRECISION) + sizetable[(coder->intervalsize - 0x8000) >> 8];
	}
	int size_diff = size_after - size_before;
	
	// Trace the output state
	rangecoder_trace_state(coder, "CODE_END", context_index, bit, size_diff);
	
	return size_diff;
}

// Function pointer setup is handled inline where needed

void rangecoder_reset(RangeCoder *coder) {
	for (int i = 0; i < 1024; i++) {
		coder->contexts[i] = 0x8000;
	}
}

void rangecoder_finish(RangeCoder *coder) {
	// Trace the finish start
	if (coder->trace_file) {
		fprintf(coder->trace_file, 
			"RANGECODER: FINISH_START intervalmin=0x%04x intervalsize=0x%04x dest_bit=%d\n",
			coder->intervalmin, coder->intervalsize, coder->dest_bit);
	}
	
	// Add detailed tracing for the finish process
	if (coder->trace_file) {
		fprintf(coder->trace_file, 
			"RANGECODER: FINISH_DETAILED_START intervalmin=0x%04x intervalsize=0x%04x dest_bit=%d out_size=%d\n",
			coder->intervalmin, coder->intervalsize, coder->dest_bit, *coder->out_size);
	}
	
	int intervalmax = coder->intervalmin + coder->intervalsize;
	int final_min = 0;
	int final_size = 0x10000;
	while (final_min < coder->intervalmin || final_min + final_size >= intervalmax) {
		if (final_min + final_size < intervalmax) {
			add_bit(coder);
			final_min += final_size;
		}
		coder->dest_bit++;
		final_size >>= 1;
	}

	int required_bytes = ((coder->dest_bit - 1) >> 3) + 1;
	if (required_bytes > *coder->out_size) {
		*coder->out = realloc(*coder->out, required_bytes * sizeof(unsigned char));
		// Initialize new bytes to 0
		for (int i = *coder->out_size; i < required_bytes; i++) {
			(*coder->out)[i] = 0;
		}
		*coder->out_size = required_bytes;
	} else {
		// Update the output size to the actual data size
		*coder->out_size = required_bytes;
	}
	
	// Trace the finish end
	if (coder->trace_file) {
		fprintf(coder->trace_file, 
			"RANGECODER: FINISH_END final dest_bit=%d out_size=%d\n",
			coder->dest_bit, *coder->out_size);
	}
}

int rangecoder_size_in_bits(RangeCoder *coder) {
	return coder->dest_bit + 1;
}

// Tracing methods
void rangecoder_set_trace(RangeCoder *coder, FILE *trace_file) {
	coder->trace_file = trace_file;
}

void rangecoder_trace_state(RangeCoder *coder, const char *operation, int context, int bit, int size) {
	if (coder->trace_file) {
		fprintf(coder->trace_file, 
			"RANGECODER: %s context=%d bit=%d size=%d intervalmin=0x%04x intervalsize=0x%04x dest_bit=%d\n",
			operation, context, bit, size, coder->intervalmin, coder->intervalsize, coder->dest_bit);
	}
}

// Set function pointer after function is defined
static void set_rangecoder_function(RangeCoder *coder) {
	coder->base.code = (int (*)(void*, int, int))rangecoder_code;
}
