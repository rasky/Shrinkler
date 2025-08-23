// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Pack a data block in multiple iterations, reporting progress along the way.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Pack.h"
#include "CountingCoder.h"
#include "SizeMeasuringCoder.h"
#include "LZProgress.h"

void packData(unsigned char *data, int data_length, int zero_padding, PackParams *params, Coder *result_coder, RefEdgeFactory *edge_factory, int show_progress, FILE *trace_file) {
	printf("%8d", data_length);
	
	// Create match finder
	MatchFinder *finder = matchfinder_new(data, data_length, 2, params->match_patience, params->max_same_length);
	if (!finder) {
		fprintf(stderr, "Failed to create match finder\n");
		return;
	}
	
	// Create LZ parser
	LZParser *parser = lzparser_new(data, data_length, zero_padding, finder, params->length_margin, params->skip_length, edge_factory);
	if (!parser) {
		fprintf(stderr, "Failed to create LZ parser\n");
		matchfinder_free(finder);
		return;
	}
	
	// Initialize variables for iteration
	unsigned long long real_size = 0;
	unsigned long long best_size = (unsigned long long)1 << (32 + 3 + 8); // BIT_PRECISION = 8
	int best_result = 0;
	LZParseResult results[2];
	results[0].edges = NULL;
	results[0].edges_count = 0;
	results[1].edges = NULL;
	results[1].edges_count = 0;
	
	// Create counting coder
	CountingCoder *counting_coder = countingcoder_new(NUM_CONTEXTS);
	if (!counting_coder) {
		fprintf(stderr, "Failed to create counting coder\n");
		lzparser_free(parser);
		matchfinder_free(finder);
		return;
	}
	
	// Create progress indicator
	void *progress;
	if (show_progress) {
		progress = packprogress_new();
	} else {
		progress = noprogress_new();
	}
	
	// Main iteration loop
	for (int i = 0; i < params->iterations; i++) {
		printf("  ");
		
		// Parse data into LZ symbols
		LZParseResult *result = &results[1 - best_result];
		
		// Free previous result edges
		if (result->edges) {
			free(result->edges);
			result->edges = NULL;
			result->edges_count = 0;
		}
		
		SizeMeasuringCoder *measurer = sizemeasuringcoder_new(counting_coder);
		if (!measurer) {
			fprintf(stderr, "Failed to create size measuring coder\n");
			break;
		}
		
		// Set number contexts
		coder_set_number_contexts((Coder*)measurer, NUMBER_CONTEXT_OFFSET, NUM_NUMBER_CONTEXTS, data_length);
		
		// Reset match finder
		matchfinder_reset(finder);
		
		// Parse with progress
		LZEncoder *parse_encoder = lzencoder_new((Coder*)measurer, params->parity_context);
		if (trace_file) {
			lzparser_set_trace(parser, trace_file);
		}
		*result = lzparser_parse(parser, parse_encoder, progress);
		lzencoder_free(parse_encoder);
		
		// Clean up measurer
		sizemeasuringcoder_free(measurer);
		
		// Encode result using adaptive range coding to measure size
		unsigned char *dummy_result = NULL;
		int dummy_size = 0;
		RangeCoder *range_coder = rangecoder_new(NUM_CONTEXTS, &dummy_result, &dummy_size);
		if (range_coder) {
			LZEncoder *range_encoder = lzencoder_new((Coder*)range_coder, params->parity_context);
			real_size = lzparseresult_encode(result, range_encoder);
			lzencoder_free(range_encoder);
			rangecoder_finish(range_coder);
			rangecoder_free(range_coder);
			free(dummy_result);
		}
		
		// Choose if best
		if (real_size < best_size) {
			best_result = 1 - best_result;
			best_size = real_size;
		}
		
		// Print size
		printf("%14.3f", real_size / (double)(8 << BIT_PRECISION));
		
		// Count symbol frequencies for next iteration
		CountingCoder *new_counting_coder = countingcoder_new(NUM_CONTEXTS);
		if (new_counting_coder) {
			LZEncoder *counting_encoder = lzencoder_new((Coder*)counting_coder, params->parity_context);
			lzparseresult_encode(result, counting_encoder);
			lzencoder_free(counting_encoder);
			
			// Merge counting coders
			CountingCoder *old_counting_coder = counting_coder;
			counting_coder = countingcoder_merge(old_counting_coder, new_counting_coder);
			countingcoder_free(old_counting_coder);
			countingcoder_free(new_counting_coder);
		}
	}
	
	// Cleanup
	if (show_progress) {
		packprogress_free((PackProgress*)progress);
	} else {
		noprogress_free((NoProgress*)progress);
	}
	countingcoder_free(counting_coder);
	
	// Encode best result to output
	LZEncoder *final_encoder = lzencoder_new(result_coder, params->parity_context);
	if (trace_file) {
		lzencoder_set_trace(final_encoder, trace_file);
	}
	lzparseresult_encode(&results[best_result], final_encoder);
	lzencoder_free(final_encoder);
	
	// Cleanup results
	if (results[0].edges) {
		free(results[0].edges);
		results[0].edges = NULL;
	}
	if (results[1].edges) {
		free(results[1].edges);
		results[1].edges = NULL;
	}
	
	lzparser_free(parser);
	matchfinder_free(finder);
	
	printf("\n");
}
