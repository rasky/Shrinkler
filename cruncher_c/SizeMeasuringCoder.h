// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Size measuring coder for measuring compressed size.

*/

#pragma once

#include "Coder.h"
#include "CountingCoder.h"

typedef struct {
	Coder base;
	CountingCoder *counting_coder;
	unsigned short *context_sizes;
	int num_contexts;
} SizeMeasuringCoder;

// Function declarations
SizeMeasuringCoder* sizemeasuringcoder_new(CountingCoder *counting_coder);
void sizemeasuringcoder_free(SizeMeasuringCoder *coder);
void sizemeasuringcoder_set_number_contexts(SizeMeasuringCoder *coder, int number_context_offset, int n_number_contexts, int max_number);
