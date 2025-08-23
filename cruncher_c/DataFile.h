// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Operations on raw data files, including loading, crunching and saving.

*/

#pragma once

#include <string.h>
#include <stdlib.h>
#include "AmigaWords.h"
#include "Pack.h"
#include "RefEdge.h"

#define SHRINKLER_MAJOR_VERSION 4
#define SHRINKLER_MINOR_VERSION 7
#define FLAG_PARITY_CONTEXT (1 << 0)

typedef struct {
	char magic[4];
	char major_version;
	char minor_version;
	Word header_size;
	Longword compressed_size;
	Longword uncompressed_size;
	Longword safety_margin;
	Longword flags;
} DataHeader;

typedef struct {
	DataHeader header;
	unsigned char *data;
	int data_size;
} DataFile;

// Constructor and destructor
DataFile* datafile_new(void);
void datafile_free(DataFile *file);

// Methods
void datafile_load(DataFile *file, const char *filename);
void datafile_save(DataFile *file, const char *filename, int write_header);
int datafile_size(DataFile *file, int include_header);
DataFile* datafile_crunch(DataFile *file, PackParams *params, RefEdgeFactory *edge_factory, int show_progress, int enable_trace);
