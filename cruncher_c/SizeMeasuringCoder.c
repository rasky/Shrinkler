// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Size measuring coder for measuring compressed size.

*/

#include <stdlib.h>
#include <math.h>
#include "SizeMeasuringCoder.h"

static int sizemeasuringcoder_code(void *self, int context, int bit) {
	SizeMeasuringCoder *coder = (SizeMeasuringCoder*)self;
	if (context >= 0 && context < coder->num_contexts) {
		return coder->context_sizes[context * 2 + bit];
	}
	return 1 << BIT_PRECISION; // Default size if context out of range
}

static int size_for_count(int count, int total) {
	int size = (int)floor(0.5 + log(total / (double)count) / log(2.0) * (1 << BIT_PRECISION));
	if (size < 2) size = 2;
	if (size > 12 << BIT_PRECISION) size = 12 << BIT_PRECISION;
	return size;
}

SizeMeasuringCoder* sizemeasuringcoder_new(CountingCoder *counting_coder) {
	SizeMeasuringCoder *coder = malloc(sizeof(SizeMeasuringCoder));
	if (!coder) return NULL;
	
	coder->counting_coder = counting_coder;
	coder->num_contexts = counting_coder->num_contexts;
	coder->context_sizes = malloc(coder->num_contexts * 2 * sizeof(unsigned short));
	if (!coder->context_sizes) {
		free(coder);
		return NULL;
	}
	
	// Calculate sizes based on counts
	for (int i = 0; i < coder->num_contexts; i++) {
		int count0 = 1 + counting_coder->context_counts[i].counts[0];
		int count1 = 1 + counting_coder->context_counts[i].counts[1];
		int sum = count0 + count1;
		coder->context_sizes[i * 2] = size_for_count(count0, sum);
		coder->context_sizes[i * 2 + 1] = size_for_count(count1, sum);
	}
	
	coder->base.code = sizemeasuringcoder_code;
	coder->base.cacheable = 0;
	coder->base.has_cache = 0;
	coder->base.cache = NULL;
	coder->base.cache_sizes = NULL;
	
	return coder;
}

void sizemeasuringcoder_free(SizeMeasuringCoder *coder) {
	if (coder) {
		free(coder->context_sizes);
		free(coder);
	}
}

void sizemeasuringcoder_set_number_contexts(SizeMeasuringCoder *coder, int number_context_offset, int n_number_contexts, int max_number) {
	// For now, just a stub - the C++ version uses this for caching number encoding
	// but our C version doesn't implement caching yet
	(void)coder;
	(void)number_context_offset;
	(void)n_number_contexts;
	(void)max_number;
}
