// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

An entropy coder based on range coding.

*/

#pragma once

#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "Coder.h"

#ifndef ADJUST_SHIFT
#define ADJUST_SHIFT 4
#endif

typedef struct {
	Coder base;
	unsigned short *contexts;
	unsigned char **out;
	int *out_size;
	int dest_bit;
	unsigned intervalsize;
	unsigned intervalmin;
	FILE *trace_file;  // Add tracing capability
} RangeCoder;

// Constructor and destructor
RangeCoder* rangecoder_new(int n_contexts, unsigned char **out, int *out_size);
void rangecoder_free(RangeCoder *coder);

// Methods
void rangecoder_reset(RangeCoder *coder);
void rangecoder_finish(RangeCoder *coder);
int rangecoder_size_in_bits(RangeCoder *coder);

// Tracing methods
void rangecoder_set_trace(RangeCoder *coder, FILE *trace_file);
void rangecoder_trace_state(RangeCoder *coder, const char *operation, int context, int bit, int size);
