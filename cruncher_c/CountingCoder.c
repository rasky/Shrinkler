// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Counting coder for measuring symbol frequencies.

*/

#include <stdlib.h>
#include <string.h>
#include "CountingCoder.h"

static int countingcoder_code(void *self, int context, int bit) {
	CountingCoder *coder = (CountingCoder*)self;
	if (context >= 0 && context < coder->num_contexts) {
		coder->context_counts[context].counts[bit]++;
	}
	return 0; // Counting coder doesn't actually encode
}

CountingCoder* countingcoder_new(int num_contexts) {
	CountingCoder *coder = malloc(sizeof(CountingCoder));
	if (!coder) return NULL;
	
	coder->context_counts = calloc(num_contexts, sizeof(ContextCounts));
	if (!coder->context_counts) {
		free(coder);
		return NULL;
	}
	
	coder->num_contexts = num_contexts;
	coder->base.code = countingcoder_code;
	coder->base.cacheable = 0;
	coder->base.has_cache = 0;
	coder->base.cache = NULL;
	coder->base.cache_sizes = NULL;
	
	return coder;
}

void countingcoder_free(CountingCoder *coder) {
	if (coder) {
		free(coder->context_counts);
		free(coder);
	}
}

void countingcoder_reset(CountingCoder *coder) {
	if (coder && coder->context_counts) {
		memset(coder->context_counts, 0, coder->num_contexts * sizeof(ContextCounts));
	}
}

CountingCoder* countingcoder_merge(CountingCoder *old_coder, CountingCoder *new_coder) {
	if (!old_coder || !new_coder || old_coder->num_contexts != new_coder->num_contexts) {
		return NULL;
	}
	
	CountingCoder *merged = countingcoder_new(old_coder->num_contexts);
	if (!merged) return NULL;
	
	// Merge counts using weighted average (75% old + 25% new) like C++ version
	for (int i = 0; i < merged->num_contexts; i++) {
		merged->context_counts[i].counts[0] = (old_coder->context_counts[i].counts[0] * 3 + new_coder->context_counts[i].counts[0]) / 4;
		merged->context_counts[i].counts[1] = (old_coder->context_counts[i].counts[1] * 3 + new_coder->context_counts[i].counts[1]) / 4;
	}
	
	return merged;
}
