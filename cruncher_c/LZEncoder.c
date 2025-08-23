// Copyright 1999-2015 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

The LZ encoder defines the encoding of LZ symbols (literal bytes and references) into data bytes.

*/

#include <assert.h>
#include <stdlib.h>
#include "LZEncoder.h"

LZEncoder* lzencoder_new(Coder *coder, int parity_context) {
	LZEncoder *encoder = malloc(sizeof(LZEncoder));
	if (!encoder) return NULL;
	
	encoder->coder = coder;
	encoder->parity_mask = parity_context ? 1 : 0;
	encoder->trace_file = NULL;  // Initialize trace file to NULL
	
	return encoder;
}

void lzencoder_free(LZEncoder *encoder) {
	if (encoder) {
		free(encoder);
	}
}

void lzencoder_set_initial_state(LZEncoder *encoder, LZState *state) {
	state->after_first = 0; // Match C++ version
	state->prev_was_ref = 0;
	state->parity = 0;
	state->last_offset = 0;
}

void lzencoder_construct_state(LZEncoder *encoder, LZState *state, int pos, int prev_was_ref, int last_offset) {
	state->after_first = pos > 0;
	state->prev_was_ref = prev_was_ref;
	state->parity = pos;
	state->last_offset = last_offset;
}

int lzencoder_encode_literal(LZEncoder *encoder, unsigned char value, const LZState *state_before, LZState *state_after) {
	int parity_offset = (state_before->parity & encoder->parity_mask) << 8;
	int size = 0;
	
	// Trace the literal encoding start
	lzencoder_trace_state(encoder, "LITERAL_START", state_before->parity, value, 0);
	
	if (state_before->after_first) {
		int kind_size = coder_code(encoder->coder, 1 + CONTEXT_KIND + parity_offset, KIND_LIT);
		size += kind_size;
		lzencoder_trace_decision(encoder, "KIND_LIT", state_before->parity, CONTEXT_KIND + parity_offset, KIND_LIT, kind_size);
	}
	
	int context = 1;
	for (int i = 7 ; i >= 0 ; i--) {
		int bit = ((value >> i) & 1);
		int actual_context = 1 + (parity_offset | context);
		int bit_size = coder_code(encoder->coder, actual_context, bit);
		
		// Add detailed function call tracing
		if (encoder->trace_file && (parity_offset | context) == 257) {
			fprintf(encoder->trace_file, 
				"LZENCODER: FUNCTION_CALL context=0x%04x actual_context=%d bit=%d\n",
				parity_offset | context, actual_context, bit);
		}
		size += bit_size;
		lzencoder_trace_decision(encoder, "LITERAL_BIT", state_before->parity, parity_offset | context, bit, bit_size);
		
		// Add detailed context tracing
		if (encoder->trace_file && (parity_offset | context) == 257) {
			fprintf(encoder->trace_file, 
				"LZENCODER: CONTEXT_DETAIL pos=%d value=%d bit_pos=%d context=0x%04x actual_context=%d bit=%d\n",
				state_before->parity, value, i, parity_offset | context, 1 + (parity_offset | context), bit);
		}
		
		// Add detailed bit extraction tracing
		if (encoder->trace_file && (parity_offset | context) == 257) {
			unsigned char value_uchar = value;
			int bit_extracted = ((value_uchar >> i) & 1);
			fprintf(encoder->trace_file, 
				"LZENCODER: BIT_EXTRACTION pos=%d value=%d value_uchar=%d i=%d (value>>i)=%d (value>>i)&1=%d bit=%d\n",
				state_before->parity, value, value_uchar, i, (value_uchar >> i), bit_extracted, bit);
		}
		context = (context << 1) | bit;
	}

	state_after->after_first = 1;
	state_after->prev_was_ref = 0;
	state_after->parity = state_before->parity + 1;
	state_after->last_offset = state_before->last_offset;

	// Trace the literal encoding end
	lzencoder_trace_state(encoder, "LITERAL_END", state_before->parity, value, size);

	return size;
}

int lzencoder_encode_reference(LZEncoder *encoder, int offset, int length, const LZState *state_before, LZState *state_after) {
	assert(offset >= 1);
	assert(length >= 2);
	assert(state_before->after_first);

	// Trace the reference encoding start
	lzencoder_trace_state(encoder, "REFERENCE_START", state_before->parity, offset, length);

	int parity_offset = (state_before->parity & encoder->parity_mask) << 8;
	int size = coder_code(encoder->coder, 1 + CONTEXT_KIND + parity_offset, KIND_REF);
	lzencoder_trace_decision(encoder, "KIND_REF", state_before->parity, CONTEXT_KIND + parity_offset, KIND_REF, size);
	
	int rep_offset = offset == state_before->last_offset;
	if (!state_before->prev_was_ref) {
		int rep_size = coder_code(encoder->coder, 1 + CONTEXT_REPEATED, rep_offset);
		size += rep_size;
		lzencoder_trace_decision(encoder, "REPEATED", state_before->parity, CONTEXT_REPEATED, rep_offset, rep_size);
	} else {
		assert(!rep_offset);
	}
	
	if (!rep_offset) {
		int offset_size = coder_encode_number(encoder->coder, 1 + (CONTEXT_GROUP_OFFSET << 8), offset + 2);
		size += offset_size;
		lzencoder_trace_state(encoder, "OFFSET_NUMBER", state_before->parity, offset + 2, offset_size);
	}
	int length_size = coder_encode_number(encoder->coder, 1 + (CONTEXT_GROUP_LENGTH << 8), length);
	size += length_size;
	lzencoder_trace_state(encoder, "LENGTH_NUMBER", state_before->parity, length, length_size);

	state_after->after_first = 1;
	state_after->prev_was_ref = 1;
	state_after->parity = state_before->parity + length;
	state_after->last_offset = offset;

	// Trace the reference encoding end
	lzencoder_trace_state(encoder, "REFERENCE_END", state_before->parity, offset, size);

	return size;
}

int lzencoder_finish(LZEncoder *encoder, const LZState *state_before) {
	// Trace the finish start
	lzencoder_trace_state(encoder, "FINISH_START", state_before->parity, 0, 0);
	
	int parity_offset = (state_before->parity & encoder->parity_mask) << 8;
	int size = coder_code(encoder->coder, 1 + CONTEXT_KIND + parity_offset, KIND_REF);
	lzencoder_trace_decision(encoder, "FINISH_KIND_REF", state_before->parity, CONTEXT_KIND + parity_offset, KIND_REF, size);
	
	if (!state_before->prev_was_ref) {
		int rep_size = coder_code(encoder->coder, 1 + CONTEXT_REPEATED, 0);
		size += rep_size;
		lzencoder_trace_decision(encoder, "FINISH_REPEATED", state_before->parity, CONTEXT_REPEATED, 0, rep_size);
	}
	
	int context_group = CONTEXT_GROUP_OFFSET;
	int number = 2;
	int number_size = coder_encode_number(encoder->coder, 1 + (context_group << 8), number);
	size += number_size;
	lzencoder_trace_state(encoder, "FINISH_NUMBER", state_before->parity, number, number_size);

	// Trace the finish end
	lzencoder_trace_state(encoder, "FINISH_END", state_before->parity, 0, size);

	return size;
}

// Tracing methods
void lzencoder_set_trace(LZEncoder *encoder, FILE *trace_file) {
	encoder->trace_file = trace_file;
}

void lzencoder_trace_state(LZEncoder *encoder, const char *operation, int pos, int value, int size) {
	if (encoder->trace_file) {
		fprintf(encoder->trace_file, 
			"LZENCODER: %s pos=%d value=%d size=%d\n",
			operation, pos, value, size);
	}
}

void lzencoder_trace_decision(LZEncoder *encoder, const char *operation, int pos, int context, int bit, int size) {
	if (encoder->trace_file) {
		fprintf(encoder->trace_file, 
			"LZENCODER: %s pos=%d context=%d bit=%d size=%d\n",
			operation, pos, context, bit, size);
	}
}
