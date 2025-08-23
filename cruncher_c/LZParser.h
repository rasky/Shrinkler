// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

LZ parser for compression.

*/

#pragma once

#include "LZEncoder.h"
#include "MatchFinder.h"
#include "RefEdge.h"
#include "Heap.h"
#include "CuckooHash.h"

typedef struct {
	int pos;
	int offset;
	int length;
} LZResultEdge;

typedef struct {
	LZResultEdge *edges;
	int edges_count;
	const unsigned char *data;
	int data_length;
	int zero_padding;
} LZParseResult;

typedef struct {
	const unsigned char *data;
	int data_length;
	int zero_padding;
	MatchFinder *finder;
	int length_margin;
	int skip_length;
	RefEdgeFactory *edge_factory;
	LZEncoder *encoder;
	int *literal_size;
	FILE *trace_file;  // Add tracing capability
	
	// Dynamic programming structures
	CuckooHash **edges_to_pos;
	RefEdge *best;
	CuckooHash *best_for_offset;
	Heap *root_edges;
} LZParser;

// Function declarations
LZParser* lzparser_new(unsigned char *data, int data_length, int zero_padding, MatchFinder *finder, int length_margin, int skip_length, RefEdgeFactory *edge_factory);
void lzparser_free(LZParser *parser);
LZParseResult lzparser_parse(LZParser *parser, LZEncoder *encoder, void *progress);
unsigned long long lzparseresult_encode(LZParseResult *result, LZEncoder *encoder);

// Tracing methods
void lzparser_set_trace(LZParser *parser, FILE *trace_file);
void lzparser_trace_decision(LZParser *parser, int pos, int offset, int length, int total_size, const char *reason);
void lzparser_trace_match(LZParser *parser, int pos, int match_pos, int match_length);
void lzparser_trace_literal(LZParser *parser, int pos, unsigned char value, int size);
