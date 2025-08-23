// Copyright 1999-2019 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Parse a data block into LZ symbols (literal bytes and references).

*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "LZParser.h"
#include "LZProgress.h"

LZParser* lzparser_new(unsigned char *data, int data_length, int zero_padding, MatchFinder *finder, int length_margin, int skip_length, RefEdgeFactory *edge_factory) {
	LZParser *parser = malloc(sizeof(LZParser));
	if (!parser) return NULL;
	
	parser->data = data;
	parser->data_length = data_length;
	parser->zero_padding = zero_padding;
	parser->finder = finder;
	parser->length_margin = length_margin;
	parser->skip_length = skip_length;
	parser->edge_factory = edge_factory;
	parser->encoder = NULL;
	parser->trace_file = NULL;  // Initialize trace file to NULL
	
	// Allocate literal size array
	parser->literal_size = malloc((data_length + 1) * sizeof(int));
	if (!parser->literal_size) {
		free(parser);
		return NULL;
	}
	
	// Initialize dynamic programming structures
	parser->edges_to_pos = malloc((data_length + 1) * sizeof(CuckooHash*));
	if (!parser->edges_to_pos) {
		free(parser->literal_size);
		free(parser);
		return NULL;
	}
	
	for (int i = 0; i <= data_length; i++) {
		parser->edges_to_pos[i] = cuckoohash_new(1000); // Larger capacity for each position
		if (!parser->edges_to_pos[i]) {
			// Clean up on failure
			for (int j = 0; j < i; j++) {
				cuckoohash_free(parser->edges_to_pos[j]);
			}
			free(parser->edges_to_pos);
			free(parser->literal_size);
			free(parser);
			return NULL;
		}
	}
	
	parser->best = NULL;
	parser->best_for_offset = cuckoohash_new(50000); // Larger capacity for best edges
	parser->root_edges = heap_new(200000); // Increase capacity for larger files
	
	if (!parser->best_for_offset || !parser->root_edges) {
		// Clean up on failure
		for (int i = 0; i <= data_length; i++) {
			cuckoohash_free(parser->edges_to_pos[i]);
		}
		free(parser->edges_to_pos);
		free(parser->literal_size);
		cuckoohash_free(parser->best_for_offset);
		heap_free(parser->root_edges);
		free(parser);
		return NULL;
	}
	
	return parser;
}

void lzparser_free(LZParser *parser) {
	if (parser) {
		free(parser->literal_size);
		
		if (parser->edges_to_pos) {
			for (int i = 0; i <= parser->data_length; i++) {
				cuckoohash_free(parser->edges_to_pos[i]);
			}
			free(parser->edges_to_pos);
		}
		
		cuckoohash_free(parser->best_for_offset);
		heap_free(parser->root_edges);
		free(parser);
	}
}

static int is_root(LZParser *parser, RefEdge *edge) {
	return heap_contains(parser->root_edges, edge);
}

static void remove_root(LZParser *parser, RefEdge *edge) {
	heap_remove(parser->root_edges, edge);
}

static void release_edge(LZParser *parser, RefEdge *edge, int clean) {
	while (edge != NULL) {
		RefEdge *source = edge->source;
		if (--edge->refcount == 0) {
			assert(!is_root(parser, edge));
			refedgefactory_destroy(parser->edge_factory, edge, clean);
		} else {
			return;
		}
		edge = source;
	}
}

static int clean_worst_edge(LZParser *parser, int pos, RefEdge *exclude) {
	if (heap_empty(parser->root_edges)) return 0;
	
	RefEdge *worst_edge = heap_remove_largest(parser->root_edges);
	if (worst_edge == parser->best || worst_edge == exclude) return 1;
	
	CuckooHash *container = (refedge_target(worst_edge) > pos) ? 
		parser->edges_to_pos[refedge_target(worst_edge)] : parser->best_for_offset;
	
	if (cuckoohash_count(container, worst_edge->offset) > 0) {
		cuckoohash_erase(container, worst_edge->offset);
		release_edge(parser, worst_edge, 1);
	}
	
	return 1;
}

static void put_by_offset(LZParser *parser, CuckooHash *by_offset, RefEdge *edge) {
	assert(!is_root(parser, edge));
	
	int count = cuckoohash_count(by_offset, edge->offset);
	
	if (count == 0) {
		cuckoohash_insert(by_offset, edge->offset, edge);
		heap_insert(parser->root_edges, edge);
	} else if (edge->total_size < cuckoohash_get(by_offset, edge->offset)->total_size) {
		RefEdge *old_edge = cuckoohash_get(by_offset, edge->offset);
		remove_root(parser, old_edge);
		release_edge(parser, old_edge, 0);
		cuckoohash_insert(by_offset, edge->offset, edge);
		heap_insert(parser->root_edges, edge);
	} else {
		release_edge(parser, edge, 0);
	}
}

static void new_edge(LZParser *parser, RefEdge *source, int pos, int offset, int length) {
	if (source && offset == source->offset && pos == refedge_target(source)) return;
	
	int prev_target = source ? refedge_target(source) : 0;
	int new_target = pos + length;
	
	LZState state_before;
	LZState state_after;
	lzencoder_construct_state(parser->encoder, &state_before, pos, pos == prev_target, source ? source->offset : 0);
	
	int size_before = (source ? source->total_size : parser->literal_size[parser->data_length]) - 
		(parser->literal_size[parser->data_length] - parser->literal_size[pos]);
	
	int edge_size = lzencoder_encode_reference(parser->encoder, offset, length, &state_before, &state_after);
	int size_after = parser->literal_size[parser->data_length] - parser->literal_size[new_target];
	
	while (refedgefactory_full(parser->edge_factory)) {
		if (!clean_worst_edge(parser, pos, source)) break;
	}
	
	RefEdge *new_edge = refedgefactory_create(parser->edge_factory, pos, offset, length, 
		size_before + edge_size + size_after, source);
	
	put_by_offset(parser, parser->edges_to_pos[new_target], new_edge);
	
	// Trace the edge creation
	lzparser_trace_decision(parser, pos, offset, length, size_before + edge_size + size_after, "NEW_EDGE");
	
	// Enhanced default tracing: log edge creation with source info
	if (parser->trace_file) {
		fprintf(parser->trace_file,
			"LZPARSER: EDGE_CREATED pos=%d offset=%d length=%d total_cost=%d source_offset=%d source_pos=%d\n",
			pos, offset, length, size_before + edge_size + size_after,
			source ? source->offset : -1, source ? source->pos : -1);
	}
}

LZParseResult lzparser_parse(LZParser *parser, LZEncoder *encoder, void *progress) {
	LZProgress *prog = (LZProgress*)progress;
	if (prog) {
		prog->begin(progress, parser->data_length);
	}
	
	parser->encoder = encoder;
	
	// Reset state
	cuckoohash_clear(parser->best_for_offset);
	heap_clear(parser->root_edges);
	refedgefactory_reset(parser->edge_factory);
	
	// Accumulate literal sizes
	int size = 0;
	LZState literal_state;
	lzencoder_set_initial_state(encoder, &literal_state);
	
	for (int i = 0; i < parser->data_length; i++) {
		parser->literal_size[i] = size;
		size += lzencoder_encode_literal(encoder, parser->data[i], &literal_state, &literal_state);
	}
	parser->literal_size[parser->data_length] = size;
	
			// Parse
		RefEdge *initial_best = refedgefactory_create(parser->edge_factory, 0, 0, 0, parser->literal_size[parser->data_length], NULL);
		parser->best = initial_best;

	
	for (int pos = 1; pos <= parser->data_length; pos++) {
		// Assimilate edges ending here
		if (parser->trace_file) {
			fprintf(parser->trace_file,
				"LZPARSER: ASSIMILATE_START pos=%d best_offset=%d best_total=%d edges_count=%d\n",
				pos, parser->best->offset, parser->best->total_size, parser->edges_to_pos[pos]->size);
		}
		CuckooHashIterator it = cuckoohash_begin(parser->edges_to_pos[pos]);
		while (cuckoohash_iterator_valid(&it)) {
			RefEdge *edge = cuckoohash_iterator_value(&it);
			if (parser->trace_file) {
				fprintf(parser->trace_file,
					"LZPARSER: ASSIMILATE_EDGE pos=%d edge_offset=%d edge_total=%d best_total=%d will_update=%d\n",
					pos, edge->offset, edge->total_size, parser->best->total_size, 
					edge->total_size < parser->best->total_size);
			}
			if (edge->total_size < parser->best->total_size || 
				(edge->total_size == parser->best->total_size && edge->offset < parser->best->offset)) {
				parser->best = edge;
				if (parser->trace_file) {
					fprintf(parser->trace_file,
						"LZPARSER: BEST_UPDATED pos=%d new_best_offset=%d new_best_total=%d\n",
						pos, parser->best->offset, parser->best->total_size);
				}
			}
			remove_root(parser, edge);
			put_by_offset(parser, parser->best_for_offset, edge);
			cuckoohash_iterator_next(&it);
		}
		cuckoohash_clear(parser->edges_to_pos[pos]);
		
		// Add new edges according to matches
		matchfinder_begin_matching(parser->finder, pos);
		int match_pos, match_length;
		int max_match_length = 0;

		
		while (matchfinder_next_match(parser->finder, &match_pos, &match_length)) {
			int offset = pos - match_pos;
			if (match_length > parser->data_length - pos) {
				match_length = parser->data_length - pos;
			}
			
			int min_length = match_length - parser->length_margin;
			if (min_length < 2) min_length = 2;
			
							for (int length = min_length; length <= match_length; length++) {
					// Enhanced default tracing: log edge creation attempts
					if (parser->trace_file) {
						fprintf(parser->trace_file,
							"LZPARSER: EDGE_ATTEMPT pos=%d offset=%d length=%d best_offset=%d\n",
							pos, offset, length, parser->best->offset);
					}
					new_edge(parser, parser->best, pos, offset, length);
					// Enhanced default tracing: log condition evaluation
					if (parser->trace_file) {
						fprintf(parser->trace_file,
							"LZPARSER: CONDITION_EVAL pos=%d offset=%d length=%d best_offset=%d count=%d condition=%d\n",
							pos, offset, length, parser->best->offset, cuckoohash_count(parser->best_for_offset, offset),
							(parser->best->offset != offset && cuckoohash_count(parser->best_for_offset, offset)));
					}
					if (parser->best->offset != offset && cuckoohash_count(parser->best_for_offset, offset)) {
						// Enhanced default tracing: log second edge creation
						if (parser->trace_file) {
							fprintf(parser->trace_file,
								"LZPARSER: SECOND_EDGE pos=%d offset=%d length=%d existing_offset=%d\n",
								pos, offset, length, cuckoohash_get(parser->best_for_offset, offset)->offset);
						}
						assert(cuckoohash_get(parser->best_for_offset, offset)->pos <= pos);
						new_edge(parser, cuckoohash_get(parser->best_for_offset, offset), pos, offset, length);
					}
				}
			max_match_length = (match_length > max_match_length) ? match_length : max_match_length;
			lzparser_trace_match(parser, pos, match_pos, match_length);
		}
		
		// If we have a very long match, skip ahead
		if (max_match_length >= parser->skip_length && !cuckoohash_empty(parser->edges_to_pos[pos + max_match_length])) {
			heap_clear(parser->root_edges);
			
			CuckooHashIterator it = cuckoohash_begin(parser->best_for_offset);
			while (cuckoohash_iterator_valid(&it)) {
				release_edge(parser, cuckoohash_iterator_value(&it), 0);
				cuckoohash_iterator_next(&it);
			}
			cuckoohash_clear(parser->best_for_offset);
			
			int target_pos = pos + max_match_length;
			while (pos < target_pos - 1) {
				CuckooHash *edges = parser->edges_to_pos[++pos];
				CuckooHashIterator it = cuckoohash_begin(edges);
				while (cuckoohash_iterator_valid(&it)) {
					release_edge(parser, cuckoohash_iterator_value(&it), 0);
					cuckoohash_iterator_next(&it);
				}
				cuckoohash_clear(edges);
			}
			parser->best = initial_best;
		}
		

		
		if (prog) {
			prog->update(progress, pos);
		}
	}
	
	// Clean unused paths
	heap_clear(parser->root_edges);
	
	CuckooHashIterator it = cuckoohash_begin(parser->best_for_offset);
	while (cuckoohash_iterator_valid(&it)) {
		RefEdge *edge = cuckoohash_iterator_value(&it);
		if (edge != parser->best) {
			release_edge(parser, edge, 0);
		}
		cuckoohash_iterator_next(&it);
	}
	
	// Find best path
	LZParseResult result;
	result.data = parser->data;
	result.data_length = parser->data_length;
	result.zero_padding = parser->zero_padding;
	result.edges = NULL;
	result.edges_count = 0;
	

	

	

	
	RefEdge *edge = parser->best;
	while (edge->length > 0) {
		// Allocate more space for edges
		result.edges = realloc(result.edges, (result.edges_count + 1) * sizeof(LZResultEdge));
		if (!result.edges) {
			// Handle allocation failure
			return result;
		}
		
		result.edges[result.edges_count].pos = edge->pos;
		result.edges[result.edges_count].offset = edge->offset;
		result.edges[result.edges_count].length = edge->length;
		
		lzparser_trace_decision(parser, edge->pos, edge->offset, edge->length, edge->total_size, "FINAL_CHOICE");

		result.edges_count++;
		
		edge = edge->source;
	}

	
	release_edge(parser, edge, 0);
	release_edge(parser, parser->best, 0);
	
	if (prog) {
		prog->end(progress);
	}
	
	return result;
}

unsigned long long lzparseresult_encode(LZParseResult *result, LZEncoder *encoder) {
	unsigned long long size = 0;
	int pos = 0;
	LZState state;
	lzencoder_set_initial_state(encoder, &state);
	
	// Encode edges in reverse order (like C++ version)
	for (int i = result->edges_count - 1; i >= 0; i--) {
		const LZResultEdge *edge = &result->edges[i];
		while (pos < edge->pos) {
			size += lzencoder_encode_literal(encoder, result->data[pos++], &state, &state);
		}
		size += lzencoder_encode_reference(encoder, edge->offset, edge->length, &state, &state);
		pos += edge->length;
	}
	
	while (pos < result->data_length) {
		size += lzencoder_encode_literal(encoder, result->data[pos++], &state, &state);
	}
	
	if (result->zero_padding > 0) {
		size += lzencoder_encode_literal(encoder, 0, &state, &state);
		if (result->zero_padding == 2) {
			size += lzencoder_encode_literal(encoder, 0, &state, &state);
		} else if (result->zero_padding > 1) {
			size += lzencoder_encode_reference(encoder, 1, result->zero_padding - 1, &state, &state);
		}
	}
	
	size += lzencoder_finish(encoder, &state);
	return size;
}

// Tracing methods
void lzparser_set_trace(LZParser *parser, FILE *trace_file) {
	parser->trace_file = trace_file;
}

void lzparser_trace_decision(LZParser *parser, int pos, int offset, int length, int total_size, const char *reason) {
	if (parser->trace_file) {
		fprintf(parser->trace_file, 
			"LZPARSER: DECISION pos=%d offset=%d length=%d total_size=%d reason=%s\n",
			pos, offset, length, total_size, reason);
	}
}

void lzparser_trace_match(LZParser *parser, int pos, int match_pos, int match_length) {
	if (parser->trace_file) {
		fprintf(parser->trace_file, 
			"LZPARSER: MATCH pos=%d match_pos=%d match_length=%d offset=%d\n",
			pos, match_pos, match_length, pos - match_pos);
	}
}

void lzparser_trace_literal(LZParser *parser, int pos, unsigned char value, int size) {
	if (parser->trace_file) {
		fprintf(parser->trace_file, 
			"LZPARSER: LITERAL pos=%d value=%d size=%d\n",
			pos, value, size);
	}
}

