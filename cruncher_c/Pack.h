// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Pack a data block in multiple iterations, reporting progress along the way.

*/

#pragma once

#include "RangeCoder.h"
#include "MatchFinder.h"
#include "CountingCoder.h"
#include "SizeMeasuringCoder.h"
#include "LZEncoder.h"
#include "LZParser.h"

typedef struct {
	int parity_context;
	int iterations;
	int length_margin;
	int skip_length;
	int match_patience;
	int max_same_length;
} PackParams;



void packData(unsigned char *data, int data_length, int zero_padding, PackParams *params, Coder *result_coder, RefEdgeFactory *edge_factory, int show_progress, FILE *trace_file);
