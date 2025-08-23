// Copyright 1999-2019 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Suffix array construction based on the SA-IS algorithm.

*/

#pragma once

#define UNINITIALIZED (-1)
#define IS_LMS(i, stype) ((i) > 0 && (stype)[(i)] && !(stype)[(i) - 1])

// Compute the suffix array of a string over an integer alphabet.
// The last character in the string (the sentinel) must be uniquely smallest in the string.
void computeSuffixArray(const int *data, int *suffix_array, int length, int alphabet_size);
