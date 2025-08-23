// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Match finder for LZ compression.

*/

#pragma once

// Simple heap for match buffer
typedef struct {
	int *data;
	int size;
	int capacity;
} IntHeap;

typedef struct {
	// Inputs
	unsigned char *data;
	int length;
	int min_length;
	int match_patience;
	int max_same_length;
	
	// Suffix array
	int *suffix_array;
	int *rev_suffix_array;
	int *longest_common_prefix;
	
	// Matcher parameters
	int current_pos;
	int min_pos;
	
	// Matcher state
	int left_index;
	int left_length;
	int right_index;
	int right_length;
	int current_length;
	
	// Best matches seen with current length
	IntHeap match_buffer;
} MatchFinder;

// Function declarations
MatchFinder* matchfinder_new(unsigned char *data, int length, int min_length, int match_patience, int max_same_length);
void matchfinder_free(MatchFinder *finder);
void matchfinder_reset(MatchFinder *finder);
void matchfinder_begin_matching(MatchFinder *finder, int pos);
int matchfinder_next_match(MatchFinder *finder, int *match_pos_out, int *match_length_out);
