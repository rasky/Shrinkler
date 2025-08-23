// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Main file for the cruncher.

*/

//#define SHRINKLER_TITLE ("Shrinkler executable file compressor by Blueberry - version 4.7 (2022-02-22)\n\n")

#ifndef SHRINKLER_TITLE
#define SHRINKLER_TITLE ("Shrinkler executable file compressor by Blueberry - development version (built " __DATE__ " " __TIME__ ")\n\n")
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "HunkFile.h"
#include "DataFile.h"

void usage() {
	printf("Usage: Shrinkler <options> <input executable> <output executable>\n");
	printf("\n");
	printf("Available options are (default values in parentheses):\n");
	printf(" -d, --data           Treat input as raw data, rather than executable\n");
	printf(" -b, --bytes          Disable parity context - better on byte-oriented data\n");
	printf(" -w, --header         Write data file header for easier loading\n");
	printf(" -h, --hunkmerge      Merge hunks of the same memory type\n");
	printf(" -u, --no-crunch      Process hunks without crunching\n");
	printf(" -o, --overlap        Overlap compressed and decompressed data to save memory\n");
	printf(" -m, --mini           Use a smaller, but more restricted decrunch header\n");
	printf(" -c, --commandline    Support passing commandline arguments to the program\n");
	printf(" -1, ..., -9          Presets for all compression options (-3)\n");
	printf(" -i, --iterations     Number of iterations for the compression (3)\n");
	printf(" -l, --length-margin  Number of shorter matches considered for each match (3)\n");
	printf(" -a, --same-length    Number of matches of the same length to consider (30)\n");
	printf(" -e, --effort         Perseverance in finding multiple matches (300)\n");
	printf(" -s, --skip-length    Minimum match length to accept greedily (3000)\n");
	printf(" -r, --references     Number of reference edges to keep in memory (100000)\n");
	printf(" -t, --text           Print a text, followed by a newline, before decrunching\n");
	printf(" -T, --textfile       Print the contents of the given file before decrunching\n");
	printf(" -f, --flash          Poke into a register (e.g. DFF180) during decrunching\n");
	printf(" -p, --no-progress    Do not print progress info: no ANSI codes in output\n");
	printf(" --trace              Enable detailed tracing to trace.log\n");
	printf("\n");
	exit(0);
}

typedef struct {
	int seen;
	int min_value;
	int max_value;
	int value;
} IntParameter;

typedef struct {
	int seen;
	unsigned value;
} HexParameter;

typedef struct {
	int seen;
	const char *value;
} StringParameter;

typedef struct {
	int seen;
} FlagParameter;

typedef struct {
	int seen;
	int value;
} DigitParameter;

void parse_parameter(const char *form1, const char *form2, const char *arg_kind, int argc, const char *argv[], int *consumed, int *seen) {
	*seen = 0;
	for (int i = 1 ; i < argc ; i++) {
		if (strcmp(argv[i], form1) == 0 || strcmp(argv[i], form2) == 0) {
			if (*seen) {
				printf("Error: %s specified multiple times.\n\n", argv[i]);
				usage();
			}
			consumed[i] = 1;
			if (arg_kind) {
				if (i+1 < argc && !consumed[i+1] && argv[i+1][0] != '-') {
					*seen = 1; // For now, just mark as seen
				}
				if (!*seen) {
					printf("Error: %s requires a %s argument.\n\n", argv[i], arg_kind);
					usage();
				}
				consumed[i+1] = 1;
				i = i+1;
			} else {
				*seen = 1;
			}
		}
	}
}

void init_int_parameter(IntParameter *param, const char *form1, const char *form2, int min_value, int max_value, int default_value,
                       int argc, const char *argv[], int *consumed) {
	param->min_value = min_value;
	param->max_value = max_value;
	param->value = default_value;
	parse_parameter(form1, form2, "numeric", argc, argv, consumed, &param->seen);
	
	// Parse the actual value if seen
	if (param->seen) {
		for (int i = 1 ; i < argc ; i++) {
			if (strcmp(argv[i], form1) == 0 || strcmp(argv[i], form2) == 0) {
				if (i+1 < argc && argv[i+1][0] != '-') {
					char *endptr;
					param->value = strtol(argv[i+1], &endptr, 10);
					if (endptr == &argv[i+1][strlen(argv[i+1])]) {
						if (param->value < param->min_value || param->value > param->max_value) {
							printf("Error: Argument of %s must be between %d and %d.\n\n", argv[i], param->min_value, param->max_value);
							usage();
						}
					} else {
						printf("Error: %s requires a numeric argument.\n\n", argv[i]);
						usage();
					}
				}
				break;
			}
		}
	}
}

void init_hex_parameter(HexParameter *param, const char *form1, const char *form2, int default_value,
                       int argc, const char *argv[], int *consumed) {
	param->value = default_value;
	parse_parameter(form1, form2, "hexadecimal", argc, argv, consumed, &param->seen);
	
	// Parse the actual value if seen
	if (param->seen) {
		for (int i = 1 ; i < argc ; i++) {
			if (strcmp(argv[i], form1) == 0 || strcmp(argv[i], form2) == 0) {
				if (i+1 < argc && argv[i+1][0] != '-') {
					char *endptr;
					param->value = strtol(argv[i+1], &endptr, 16);
					if (endptr != &argv[i+1][strlen(argv[i+1])]) {
						printf("Error: %s requires a hexadecimal argument.\n\n", argv[i]);
						usage();
					}
				}
				break;
			}
		}
	}
}

void init_string_parameter(StringParameter *param, const char *form1, const char *form2,
                          int argc, const char *argv[], int *consumed) {
	param->value = NULL;
	parse_parameter(form1, form2, "string", argc, argv, consumed, &param->seen);
	
	// Parse the actual value if seen
	if (param->seen) {
		for (int i = 1 ; i < argc ; i++) {
			if (strcmp(argv[i], form1) == 0 || strcmp(argv[i], form2) == 0) {
				if (i+1 < argc && argv[i+1][0] != '-') {
					param->value = argv[i+1];
				}
				break;
			}
		}
	}
}

void init_flag_parameter(FlagParameter *param, const char *form1, const char *form2,
                        int argc, const char *argv[], int *consumed) {
	parse_parameter(form1, form2, NULL, argc, argv, consumed, &param->seen);
}

void init_digit_parameter(DigitParameter *param, int default_value, int argc, const char *argv[], int *consumed) {
	param->value = default_value;
	param->seen = 0;
	for (int i = 1 ; i < argc ; i++) {
		const char *a = argv[i];
		if (strlen(a) == 2 && a[0] == '-' && a[1] >= '0' && a[1] <= '9') {
			if (param->seen) {
				printf("Error: Numeric parameter specified multiple times.\n\n");
				usage();
			}
			consumed[i] = 1;
			param->value = a[1] - '0';
			param->seen = 1;
		}
	}
}

int main2(int argc, const char *argv[]) {
	printf(SHRINKLER_TITLE);

	int *consumed = calloc(argc, sizeof(int));
	if (!consumed) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}

	DigitParameter preset;
	init_digit_parameter(&preset, 3, argc, argv, consumed);
	int p = preset.value;

	FlagParameter data;
	init_flag_parameter(&data, "-d", "--data", argc, argv, consumed);
	
	FlagParameter bytes;
	init_flag_parameter(&bytes, "-b", "--bytes", argc, argv, consumed);
	
	FlagParameter header;
	init_flag_parameter(&header, "-w", "--header", argc, argv, consumed);
	
	FlagParameter hunkmerge;
	init_flag_parameter(&hunkmerge, "-h", "--hunkmerge", argc, argv, consumed);
	
	FlagParameter no_crunch;
	init_flag_parameter(&no_crunch, "-u", "--no-crunch", argc, argv, consumed);
	
	FlagParameter overlap;
	init_flag_parameter(&overlap, "-o", "--overlap", argc, argv, consumed);
	
	FlagParameter mini;
	init_flag_parameter(&mini, "-m", "--mini", argc, argv, consumed);
	
	FlagParameter commandline;
	init_flag_parameter(&commandline, "-c", "--commandline", argc, argv, consumed);
	
	IntParameter iterations;
	init_int_parameter(&iterations, "-i", "--iterations", 1, 9, 1*p, argc, argv, consumed);
	
	IntParameter length_margin;
	init_int_parameter(&length_margin, "-l", "--length-margin", 0, 100, 1*p, argc, argv, consumed);
	
	IntParameter same_length;
	init_int_parameter(&same_length, "-a", "--same-length", 1, 100000, 10*p, argc, argv, consumed);
	
	IntParameter effort;
	init_int_parameter(&effort, "-e", "--effort", 0, 100000, 100*p, argc, argv, consumed);
	
	IntParameter skip_length;
	init_int_parameter(&skip_length, "-s", "--skip-length", 2, 100000, 1000*p, argc, argv, consumed);
	
	IntParameter references;
	init_int_parameter(&references, "-r", "--references", 1000, 100000000, 100000, argc, argv, consumed);
	
	StringParameter text;
	init_string_parameter(&text, "-t", "--text", argc, argv, consumed);
	
	StringParameter textfile;
	init_string_parameter(&textfile, "-T", "--textfile", argc, argv, consumed);
	
	HexParameter flash;
	init_hex_parameter(&flash, "-f", "--flash", 0, argc, argv, consumed);
	
	FlagParameter no_progress;
	init_flag_parameter(&no_progress, "-p", "--no-progress", argc, argv, consumed);
	
	FlagParameter trace;
	init_flag_parameter(&trace, "--trace", "--trace", argc, argv, consumed);

	// Collect files
	const char **files = malloc(argc * sizeof(const char*));
	int file_count = 0;
	
	for (int i = 1 ; i < argc ; i++) {
		if (!consumed[i]) {
			if (argv[i][0] == '-') {
				printf("Error: Unknown option %s\n\n", argv[i]);
				usage();
			}
			files[file_count++] = argv[i];
		}
	}

	if (data.seen && (commandline.seen || hunkmerge.seen || overlap.seen || mini.seen || text.seen || textfile.seen || flash.seen)) {
		printf("Error: The data option cannot be used together with any of the\n");
		printf("commandline, hunkmerge, overlap, mini, text, textfile or flash options.\n\n");
		usage();
	}

	if (bytes.seen && !data.seen) {
		printf("Error: The bytes option can only be used together with the data option.\n\n");
		usage();
	}

	if (header.seen && !data.seen) {
		printf("Error: The header option can only be used together with the data option.\n\n");
		usage();
	}

	if (no_crunch.seen && (data.seen || overlap.seen || mini.seen || preset.seen || iterations.seen || length_margin.seen || same_length.seen || effort.seen || skip_length.seen || references.seen || text.seen || textfile.seen || flash.seen)) {
		printf("Error: The no-crunch option cannot be used together with any of the\n");
		printf("crunching options.\n\n");
		usage();
	}

	if (overlap.seen && mini.seen) {
		printf("Error: The overlap and mini options cannot be used together.\n\n");
		usage();
	}

	if (text.seen && textfile.seen) {
		printf("Error: The text and textfile options cannot both be specified.\n\n");
		usage();
	}

	if (mini.seen && (text.seen || textfile.seen)) {
		printf("Error: The text and textfile options cannot be used in mini mode.\n\n");
		usage();
	}

	if (file_count == 0) {
		printf("Error: No input file specified.\n\n");
		usage();
	}
	if (file_count == 1) {
		printf("Error: No output file specified.\n\n");
		usage();
	}
	if (file_count > 2) {
		printf("Error: Too many files specified.\n\n");
		usage();
	}

	const char *infile = files[0];
	const char *outfile = files[1];

	PackParams params;
	params.parity_context = !bytes.seen;
	params.iterations = iterations.value;
	params.length_margin = length_margin.value;
	params.skip_length = skip_length.value;
	params.match_patience = effort.value;
	params.max_same_length = same_length.value;

	char *decrunch_text = NULL;
	int decrunch_text_len = 0;
	if (text.seen) {
		decrunch_text_len = strlen(text.value) + 1; // +1 for newline
		decrunch_text = malloc(decrunch_text_len + 1);
		if (!decrunch_text) {
			fprintf(stderr, "Out of memory\n");
			free(consumed);
			free(files);
			return 1;
		}
		strcpy(decrunch_text, text.value);
		decrunch_text[decrunch_text_len - 1] = '\n';
		decrunch_text[decrunch_text_len] = '\0';
	} else if (textfile.seen) {
		FILE *decrunch_text_file = fopen(textfile.value, "r");
		if (!decrunch_text_file) {
			printf("Error: Could not open text file %s\n", textfile.value);
			free(consumed);
			free(files);
			return 1;
		}
		fseek(decrunch_text_file, 0, SEEK_END);
		decrunch_text_len = ftell(decrunch_text_file);
		fseek(decrunch_text_file, 0, SEEK_SET);
		decrunch_text = malloc(decrunch_text_len + 1);
		if (!decrunch_text) {
			fprintf(stderr, "Out of memory\n");
			fclose(decrunch_text_file);
			free(consumed);
			free(files);
			return 1;
		}
		fread(decrunch_text, 1, decrunch_text_len, decrunch_text_file);
		decrunch_text[decrunch_text_len] = '\0';
		fclose(decrunch_text_file);
	}

	if (data.seen) {
		// Data file compression
		printf("Loading file %s...\n\n", infile);
		DataFile *orig = datafile_new();
		datafile_load(orig, infile);

		printf("Crunching...\n\n");
			RefEdgeFactory *edge_factory = refedgefactory_new(references.value);
	if (!edge_factory) {
		printf("Error: Failed to create edge factory\n");
		return 1;
	}
		DataFile *crunched = datafile_crunch(orig, &params, edge_factory, !no_progress.seen, trace.seen);
		datafile_free(orig);
		printf("References considered:%8d\n",  edge_factory->max_edge_count);
		printf("References discarded:%9d\n\n", edge_factory->max_cleaned_edges);

		printf("Saving file %s...\n\n", outfile);
		datafile_save(crunched, outfile, header.seen);

		printf("Final file size: %d\n\n", datafile_size(crunched, header.seen));
		
		if (edge_factory->max_edge_count > references.value) {
			printf("Note: compression may benefit from a larger reference buffer (-r option).\n\n");
		}
		
		datafile_free(crunched);
		refedgefactory_free(edge_factory);

		free(consumed);
		free(files);
		free(decrunch_text);
		return 0;
	}

	// Executable file compression
	printf("Loading file %s...\n\n", infile);
	HunkFile *orig = hunkfile_new();
	hunkfile_load(orig, infile);
	if (!hunkfile_analyze(orig)) {
		printf("\nError while analyzing input file!\n\n");
		hunkfile_free(orig);
		free(consumed);
		free(files);
		free(decrunch_text);
		return 1;
	}

	if (hunkmerge.seen) {
		printf("Merging hunks...\n\n");
		HunkFile *merged = hunkfile_merge_hunks(orig, hunkfile_merged_hunklist(orig));
		hunkfile_free(orig);
		if (!hunkfile_analyze(merged)) {
			printf("\nError while analyzing merged file!\n\n");
			hunkfile_free(merged);
			internal_error();
		}
		orig = merged;
	} else if (no_crunch.seen || hunkfile_requires_hunk_processing(orig)) {
		printf("Processing hunks...\n\n");
		HunkFile *processed = hunkfile_merge_hunks(orig, hunkfile_identity_hunklist(orig));
		hunkfile_free(orig);
		if (!hunkfile_analyze(processed)) {
			printf("\nError while analyzing processed file!\n\n");
			hunkfile_free(processed);
			internal_error();
		}
		orig = processed;
	}
	if (no_crunch.seen) {
		printf("Saving file %s...\n\n", outfile);
		hunkfile_save(orig, outfile);
#ifdef S_IRWXU // Is the POSIX file permission API available?
		chmod(outfile, 0755); // Mark file executable
#endif
		printf("Final file size: %d\n\n", hunkfile_size(orig));
		hunkfile_free(orig);

		free(consumed);
		free(files);
		free(decrunch_text);
		return 0;
	}

	if (mini.seen && !hunkfile_valid_mini(orig)) {
		printf("Input executable not suitable for mini crunching.\n"
		       "Must contain only one non-empty hunk and no relocations,\n"
		       "and the final file size must be less than 24k.\n\n");
		hunkfile_free(orig);
		free(consumed);
		free(files);
		free(decrunch_text);
		return 1;
	}
	int orig_mem = hunkfile_memory_usage(orig, 1);
	printf("Crunching...\n\n");
	RefEdgeFactory *edge_factory = refedgefactory_new(references.value);
	if (!edge_factory) {
		printf("Error: Failed to create edge factory\n");
		return 1;
	}
	HunkFile *crunched = hunkfile_crunch(orig, &params, overlap.seen, mini.seen, commandline.seen, decrunch_text, flash.value, edge_factory, !no_progress.seen, trace.seen);
	hunkfile_free(orig);
			printf("References considered:%8d\n",  edge_factory->max_edge_count);
		printf("References discarded:%9d\n\n", edge_factory->max_cleaned_edges);
	if (!hunkfile_analyze(crunched)) {
		printf("\nError while analyzing crunched file!\n\n");
		hunkfile_free(crunched);
		internal_error();
	}
	int crunched_mem_during = hunkfile_memory_usage(crunched, 1);
	int crunched_mem_after = hunkfile_memory_usage(crunched, mini.seen || overlap.seen);

	printf("Memory overhead during decrunching:  %9d\n",   crunched_mem_during - orig_mem);
	printf("Memory overhead after decrunching:   %9d\n\n", crunched_mem_after - orig_mem);

	printf("Saving file %s...\n\n", outfile);
	hunkfile_save(crunched, outfile);
#ifdef S_IRWXU // Is the POSIX file permission API available?
	chmod(outfile, 0755); // Mark file executable
#endif
	printf("Final file size: %d\n\n", hunkfile_size(crunched));
	
	if (edge_factory->max_edge_count > references.value) {
		printf("Note: compression may benefit from a larger reference buffer (-r option).\n\n");
	}
	
	hunkfile_free(crunched);
	refedgefactory_free(edge_factory);

	free(consumed);
	free(files);
	free(decrunch_text);
	return 0;
}

int main(int argc, const char *argv[]) {
	return main2(argc, argv);
}
