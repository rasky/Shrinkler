// Copyright 1999-2019 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Suffix array construction based on the SA-IS algorithm.

*/

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "SuffixArray.h"

static void induce(const int *data, int *suffix_array, int length, int alphabet_size, const int *stype, const int *buckets, int *bucket_index) {
	// Induce L suffixes
	for (int b = 0; b < alphabet_size; b++) {
		bucket_index[b] = buckets[b];
	}
	for (int s = 0; s < length; s++) {
		int index = suffix_array[s];
		if (index > 0 && !stype[index - 1]) {
			suffix_array[bucket_index[data[index - 1]]++] = index - 1;
		}
	}
	// Induce S suffixes
	for (int b = 0; b < alphabet_size; b++) {
		bucket_index[b] = buckets[b + 1];
	}
	for (int s = length - 1; s >= 0; s--) {
		int index = suffix_array[s];
		assert(index != UNINITIALIZED);
		if (index > 0 && stype[index - 1]) {
			suffix_array[--bucket_index[data[index - 1]]] = index - 1;
		}
	}
}

static int substrings_equal(const int *data, int i1, int i2, const int *stype) {
	while (data[i1++] == data[i2++]) {
		if (IS_LMS(i1, stype) && IS_LMS(i2, stype)) return 1;
	}
	return 0;
}

// Compute the suffix array of a string over an integer alphabet.
// The last character in the string (the sentinel) must be uniquely smallest in the string.
void computeSuffixArray(const int *data, int *suffix_array, int length, int alphabet_size) {
	// Handle empty string
	assert(length >= 1);
	if (length == 1) {
		suffix_array[0] = 0;
		return;
	}

	// Ottimizzazione: Alloca tutti gli array in una volta sola per ridurre le chiamate a malloc
	int total_size = length + (alphabet_size + 1) + alphabet_size;
	int *all_arrays = malloc(total_size * sizeof(int));
	if (!all_arrays) {
		return;
	}
	
	// Assegna i puntatori agli array
	int *stype = all_arrays;
	int *buckets = stype + length;
	int *bucket_index = buckets + (alphabet_size + 1);
	
	// Compute suffix types and count symbols
	stype[length - 1] = 1;
	memset(buckets, 0, (alphabet_size + 1) * sizeof(int));
	buckets[data[length - 1]] = 1;
	int is_s = 1;
	int lms_count = 0;
	for (int i = length - 2; i >= 0; i--) {
		buckets[data[i]]++;
		if (data[i] > data[i + 1]) {
			if (is_s) lms_count++;
			is_s = 0;
		} else if (data[i] < data[i + 1]) {
			is_s = 1;
		}
		stype[i] = is_s;
	}

	// Accumulate bucket sizes
	int l = 0;
	for (int b = 0; b <= alphabet_size; b++) {
		int l_next = l + buckets[b];
		buckets[b] = l;
		l = l_next;
	}
	assert(l == length);

	// Put LMS suffixes at the ends of buckets
	for (int i = 0; i < length; i++) {
		suffix_array[i] = UNINITIALIZED;
	}
	for (int b = 0; b < alphabet_size; b++) {
		bucket_index[b] = buckets[b + 1];
	}
	for (int i = length - 1; i >= 1; i--) {
		if (IS_LMS(i, stype)) {
			suffix_array[--bucket_index[data[i]]] = i;
		}
	}

	// Induce to sort LMS strings
	induce(data, suffix_array, length, alphabet_size, stype, buckets, bucket_index);

	// Compact LMS indices at the beginning of the suffix array
	int j = 0;
	for (int s = 0; s < length; s++) {
		int index = suffix_array[s];
		if (IS_LMS(index, stype)) {
			suffix_array[j++] = index;
		}
	}
	assert(j == lms_count);

	// Name LMS strings, using the second half of the suffix array
	int *sub_data = &suffix_array[length / 2];
	int sub_capacity = length - length / 2;
	for (int i = 0; i < sub_capacity; i++) {
		sub_data[i] = UNINITIALIZED;
	}
	int name = 0;
	int prev_index = UNINITIALIZED;
	for (int s = 0; s < lms_count; s++) {
		int index = suffix_array[s];
		assert(index != UNINITIALIZED);
		if (prev_index != UNINITIALIZED && !substrings_equal(data, prev_index, index, stype)) {
			name += 1;
		}
		assert(sub_data[index / 2] == UNINITIALIZED);
		sub_data[index / 2] = name;
		prev_index = index;
	}
	int new_alphabet_size = name + 1;

	if (new_alphabet_size != lms_count) {
		// Order LMS strings using suffix array of named LMS symbols

		// Compact named LMS symbols
		j = 0;
		for (int i = 0; i < sub_capacity; i++) {
			int name = sub_data[i];
			if (name != UNINITIALIZED) {
				sub_data[j++] = name;
			}
		}
		assert(j == lms_count);

		// Sort named LMS symbols recursively
		computeSuffixArray(sub_data, suffix_array, lms_count, new_alphabet_size);

		// Map named LMS symbol indices to LMS string indices in input string
		j = 0;
		for (int i = 1; i < length; i++) {
			if (IS_LMS(i, stype)) {
				sub_data[j++] = i;
			}
		}
		assert(j == lms_count);
		for (int s = 0; s < lms_count; s++) {
			assert(suffix_array[s] < lms_count);
			suffix_array[s] = sub_data[suffix_array[s]];
		}
	}

	// Put LMS suffixes in sorted order at the ends of buckets
	j = length;
	int s = lms_count - 1;
	for (int b = alphabet_size - 1; b >= 0; b--) {
		while (s >= 0 && data[suffix_array[s]] == b) {
			suffix_array[--j] = suffix_array[s--];
		}
		assert(j >= buckets[b]);
		while (j > buckets[b]) {
			suffix_array[--j] = UNINITIALIZED;
		}
	}

	// Induce from sorted LMS strings to sort all suffixes
	induce(data, suffix_array, length, alphabet_size, stype, buckets, bucket_index);

	// Cleanup - ora libera solo un array
	free(all_arrays);
}
