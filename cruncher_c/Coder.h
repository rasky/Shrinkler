// Copyright 1999-2015 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Abstract interface for entropy coding.

*/

#pragma once

#include "assert.h"
#include <stdlib.h>

// Number of fractional bits in the bit sizes returned by coding functions.
#define BIT_PRECISION 6

typedef struct Coder {
	int cacheable;
	int has_cache;
	int number_context_offset;
	int n_number_contexts;
	unsigned short **cache;
	int *cache_sizes;
	
	// Virtual function pointer
	int (*code)(void *self, int context, int bit);
} Coder;

// Code the given bit value in the given context.
// Returns the coded size of the bit (in fractional bits).
int coder_code(Coder *coder, int context, int bit);

// Encode a number >= 2 using a variable-length encoding.
// Returns the coded size of the number (in fractional bits).
int coder_encode_number(Coder *coder, int base_context, int number);

// Set parameters for number size cache
void coder_set_number_contexts(Coder *coder, int number_context_offset, int n_number_contexts, int max_number);

// Constructor and destructor
Coder* coder_new(void);
void coder_free(Coder *coder);
