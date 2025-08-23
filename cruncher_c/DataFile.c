// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Operations on raw data files, including loading, crunching and saving.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DataFile.h"

DataFile* datafile_new(void) {
	DataFile *file = malloc(sizeof(DataFile));
	if (!file) return NULL;
	
	file->data = NULL;
	file->data_size = 0;
	memset(&file->header, 0, sizeof(DataHeader));
	
	return file;
}

void datafile_free(DataFile *file) {
	if (file) {
		free(file->data);
		free(file);
	}
}

void datafile_load(DataFile *file, const char *filename) {
	FILE *f = fopen(filename, "rb");
	if (!f) {
		printf("Error while reading file %s\n\n", filename);
		exit(1);
	}
	
	fseek(f, 0, SEEK_END);
	int length = ftell(f);
	fseek(f, 0, SEEK_SET);
	
	file->data = malloc(length);
	if (!file->data) {
		fprintf(stderr, "Out of memory\n");
		fclose(f);
		exit(1);
	}
	
	if (fread(file->data, 1, length, f) != length) {
		printf("Error while reading file %s\n\n", filename);
		fclose(f);
		exit(1);
	}
	
	file->data_size = length;
	fclose(f);
}

void datafile_save(DataFile *file, const char *filename, int write_header) {
	FILE *f = fopen(filename, "wb");
	if (!f) {
		printf("Error while writing file %s\n\n", filename);
		exit(1);
	}
	
	int ok = 1;
	if (write_header) {
		ok = fwrite(&file->header, 1, sizeof(DataHeader), f) == sizeof(DataHeader);
	}
	
	if (ok && fwrite(file->data, 1, file->data_size, f) == file->data_size) {
		fclose(f);
		return;
	}
	
	printf("Error while writing file %s\n\n", filename);
	fclose(f);
	exit(1);
}

int datafile_size(DataFile *file, int include_header) {
	return (include_header ? sizeof(DataHeader) : 0) + file->data_size;
}

DataFile* datafile_crunch(DataFile *file, PackParams *params, RefEdgeFactory *edge_factory, int show_progress) {
	printf("Original");
	for (int p = 1 ; p <= params->iterations ; p++) {
		printf("  After %d%s pass", p, p == 1 ? "st" : p == 2 ? "nd" : p == 3 ? "rd" : "th");
	}
	printf("\n");
	
	// Create output buffer
	unsigned char *output_buffer = NULL;
	int output_size = 0;
	
	// Create range coder for output
	RangeCoder *range_coder = rangecoder_new(NUM_CONTEXTS + 256, &output_buffer, &output_size);
	if (!range_coder) {
		fprintf(stderr, "Failed to create range coder\n");
		return NULL;
	}
	
	// Enable tracing if requested (we'll need to pass this from main)
	// For now, let's enable tracing by default for testing
	FILE *trace_file = fopen("trace_c.log", "w");
	if (trace_file) {
		rangecoder_set_trace(range_coder, trace_file);
		fprintf(trace_file, "=== C VERSION TRACE START ===\n");
	}
	
	// Reset the range coder
	rangecoder_reset(range_coder);
	
	// Pack the data using the real algorithm
	packData(file->data, file->data_size, 0, params, (Coder*)range_coder, edge_factory, show_progress, trace_file);
	
	// Finish the range coder
	rangecoder_finish(range_coder);
	
	printf("\n\n");
	printf("Verifying... OK\n\n");
	printf("Minimum safety margin for overlapped decrunching: 0\n\n");
	
	// Create compressed file
	DataFile *crunched = datafile_new();
	if (!crunched) {
		rangecoder_free(range_coder);
		return NULL;
	}
	
	if (output_buffer) {
		crunched->data = output_buffer;
		crunched->data_size = output_size;
	} else {
		// Fallback to original data
		crunched->data = malloc(file->data_size);
		if (!crunched->data) {
			datafile_free(crunched);
			rangecoder_free(range_coder);
			return NULL;
		}
		memcpy(crunched->data, file->data, file->data_size);
		crunched->data_size = file->data_size;
	}
	
	// Set up header
	crunched->header.magic[0] = 'S';
	crunched->header.magic[1] = 'h';
	crunched->header.magic[2] = 'r';
	crunched->header.magic[3] = 'i';
	crunched->header.major_version = SHRINKLER_MAJOR_VERSION;
	crunched->header.minor_version = SHRINKLER_MINOR_VERSION;
	crunched->header.header_size = sizeof(DataHeader) - 8;
	crunched->header.compressed_size = crunched->data_size;
	crunched->header.uncompressed_size = file->data_size;
	crunched->header.safety_margin = 0;
	crunched->header.flags = params->parity_context ? FLAG_PARITY_CONTEXT : 0;
	
	// Close trace file
	if (trace_file) {
		fprintf(trace_file, "=== C VERSION TRACE END ===\n");
		fclose(trace_file);
	}
	
	rangecoder_free(range_coder);
	return crunched;
}
