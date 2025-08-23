// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

LZ encoder for compression.

*/

#pragma once

#include "Coder.h"

#define NUM_CONTEXTS 1025
#define NUM_NUMBER_CONTEXTS 16
#define NUMBER_CONTEXT_OFFSET 513

typedef struct {
	unsigned after_first:1;
	unsigned prev_was_ref:1;
	unsigned parity:1;
	unsigned last_offset:28;
} LZState;

typedef struct {
	Coder *coder;
	int parity_mask;
	FILE *trace_file;  // Add tracing capability
} LZEncoder;

// Constants
#define KIND_LIT 0
#define KIND_REF 1
#define CONTEXT_KIND 0
#define CONTEXT_REPEATED -1
#define CONTEXT_GROUP_OFFSET 2
#define CONTEXT_GROUP_LENGTH 3

// Function declarations
LZEncoder* lzencoder_new(Coder *coder, int parity_context);
void lzencoder_free(LZEncoder *encoder);
void lzencoder_set_initial_state(LZEncoder *encoder, LZState *state);
void lzencoder_construct_state(LZEncoder *encoder, LZState *state, int pos, int prev_was_ref, int last_offset);
int lzencoder_encode_literal(LZEncoder *encoder, unsigned char value, const LZState *state_before, LZState *state_after);
int lzencoder_encode_reference(LZEncoder *encoder, int offset, int length, const LZState *state_before, LZState *state_after);
int lzencoder_finish(LZEncoder *encoder, const LZState *state_before);

// Tracing methods
void lzencoder_set_trace(LZEncoder *encoder, FILE *trace_file);
void lzencoder_trace_state(LZEncoder *encoder, const char *operation, int pos, int value, int size);
void lzencoder_trace_decision(LZEncoder *encoder, const char *operation, int pos, int context, int bit, int size);
