// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Operations on Amiga executables, including loading, parsing,
hunk merging, crunching and saving.

*/

#pragma once

#include <string.h>
#include <stdlib.h>
#include "doshunks.h"
#include "AmigaWords.h"
#include "DecrunchHeaders.h"
#include "Pack.h"
#include "RefEdge.h"

#define HUNKF_MASK (HUNKF_FAST | HUNKF_CHIP)
#define NUM_RELOC_CONTEXTS 256

typedef struct {
	unsigned type;        // HUNK_<type>
	unsigned flags;       // HUNKF_<flag>
	int memsize,datasize; // longwords
	int datastart;        // longword index in file
	int relocstart;       // longword index in file
	int relocshortstart;  // longword index in file
	int relocentries;     // no. of entries
} HunkInfo;

typedef struct {
	Longword *data;
	int data_size;
	HunkInfo *hunks;
	int hunks_size;
	int relocshort_total_size;
} HunkFile;

// Constructor and destructor
HunkFile* hunkfile_new(void);
void hunkfile_free(HunkFile *file);

// Methods
void hunkfile_load(HunkFile *file, const char *filename);
void hunkfile_save(HunkFile *file, const char *filename);
int hunkfile_size(HunkFile *file);
int hunkfile_analyze(HunkFile *file);
int hunkfile_requires_hunk_processing(HunkFile *file);
int hunkfile_memory_usage(HunkFile *file, int include_last_hunk);
int hunkfile_valid_mini(HunkFile *file);

// These will be implemented later
void* hunkfile_identity_hunklist(HunkFile *file);
void* hunkfile_merged_hunklist(HunkFile *file);
HunkFile* hunkfile_merge_hunks(HunkFile *file, void *hunklist);
HunkFile* hunkfile_crunch(HunkFile *file, PackParams *params, int overlap, int mini, int commandline, char *decrunch_text, unsigned flash_address, RefEdgeFactory *edge_factory, int show_progress, int enable_trace);
