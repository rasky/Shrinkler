// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Operations on Amiga executables, including loading, parsing,
hunk merging, crunching and saving.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "HunkFile.h"

const char *hunktype[] = {
	"UNIT","NAME","CODE","DATA","BSS ","RELOC32","RELOC16","RELOC8",
	"EXT","SYMBOL","DEBUG","END","HEADER","","OVERLAY","BREAK",
	"DREL32","DREL16","DREL8","LIB","INDEX",
	"RELOC32SHORT","RELRELOC32","ABSRELOC16"
};

HunkFile* hunkfile_new(void) {
	HunkFile *file = malloc(sizeof(HunkFile));
	if (!file) return NULL;
	
	file->data = NULL;
	file->data_size = 0;
	file->hunks = NULL;
	file->hunks_size = 0;
	file->relocshort_total_size = 0;
	
	return file;
}

void hunkfile_free(HunkFile *file) {
	if (file) {
		free(file->data);
		free(file->hunks);
		free(file);
	}
}

void hunkfile_load(HunkFile *file, const char *filename) {
	FILE *f = fopen(filename, "rb");
	if (!f) {
		printf("Error while reading file %s\n\n", filename);
		exit(1);
	}
	
	fseek(f, 0, SEEK_END);
	int length = ftell(f);
	fseek(f, 0, SEEK_SET);
	
	if (length & 3) {
		printf("File %s has an illegal size!\n\n", filename);
		fclose(f);
		exit(1);
	}
	
	file->data_size = length / 4;
	file->data = malloc(length);
	if (!file->data) {
		fprintf(stderr, "Out of memory\n");
		fclose(f);
		exit(1);
	}
	
	if (fread(file->data, 4, file->data_size, f) != file->data_size) {
		printf("Error while reading file %s\n\n", filename);
		fclose(f);
		exit(1);
	}
	
	fclose(f);
}

void hunkfile_save(HunkFile *file, const char *filename) {
	FILE *f = fopen(filename, "wb");
	if (!f) {
		printf("Error while writing file %s\n\n", filename);
		exit(1);
	}
	
	if (fwrite(file->data, 4, file->data_size, f) == file->data_size) {
		fclose(f);
		return;
	}
	
	printf("Error while writing file %s\n\n", filename);
	fclose(f);
	exit(1);
}

int hunkfile_size(HunkFile *file) {
	return file->data_size * 4;
}

int hunkfile_analyze(HunkFile *file) {
	// For now, just return success
	// This will be implemented properly later
	return 1;
}

int hunkfile_requires_hunk_processing(HunkFile *file) {
	return file->relocshort_total_size != 0;
}

int hunkfile_memory_usage(HunkFile *file, int include_last_hunk) {
	int sum = 0;
	int hunks_to_sum = include_last_hunk ? file->hunks_size : file->hunks_size - 1;
	for (int h = 0 ; h < hunks_to_sum ; h++) {
		sum += ((file->hunks[h].memsize * 4 + 4) & -8) + 8;
	}
	return sum;
}

int hunkfile_valid_mini(HunkFile *file) {
	// For now, just return false
	// This will be implemented properly later
	return 0;
}

void* hunkfile_identity_hunklist(HunkFile *file) {
	// For now, just return NULL
	// This will be implemented properly later
	return NULL;
}

void* hunkfile_merged_hunklist(HunkFile *file) {
	// For now, just return NULL
	// This will be implemented properly later
	return NULL;
}

HunkFile* hunkfile_merge_hunks(HunkFile *file, void *hunklist) {
	// For now, just return a copy of the original file
	// This will be implemented properly later
	HunkFile *merged = hunkfile_new();
	if (!merged) return NULL;
	
	merged->data = malloc(file->data_size * 4);
	if (!merged->data) {
		hunkfile_free(merged);
		return NULL;
	}
	
	memcpy(merged->data, file->data, file->data_size * 4);
	merged->data_size = file->data_size;
	
	return merged;
}

HunkFile* hunkfile_crunch(HunkFile *file, PackParams *params, int overlap, int mini, int commandline, char *decrunch_text, unsigned flash_address, RefEdgeFactory *edge_factory, int show_progress) {
	// For now, just return a copy of the original file
	// This will be implemented properly later
	HunkFile *crunched = hunkfile_new();
	if (!crunched) return NULL;
	
	crunched->data = malloc(file->data_size * 4);
	if (!crunched->data) {
		hunkfile_free(crunched);
		return NULL;
	}
	
	memcpy(crunched->data, file->data, file->data_size * 4);
	crunched->data_size = file->data_size;
	
	return crunched;
}
