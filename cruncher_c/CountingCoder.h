// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Counting coder for measuring symbol frequencies.

*/

#pragma once

#include "Coder.h"

typedef struct {
	int counts[2];
} ContextCounts;

typedef struct {
	Coder base;
	int num_contexts;
	ContextCounts *context_counts;
} CountingCoder;

// Function declarations
CountingCoder* countingcoder_new(int num_contexts);
void countingcoder_free(CountingCoder *coder);
void countingcoder_reset(CountingCoder *coder);
CountingCoder* countingcoder_merge(CountingCoder *old_coder, CountingCoder *new_coder);
