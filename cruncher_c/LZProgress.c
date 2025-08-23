// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Progress reporting interface for LZ parsing.

*/

#include <stdio.h>
#include <stdlib.h>
#include "LZProgress.h"

// PackProgress implementation
static void packprogress_begin(void *self, int size) {
	PackProgress *progress = (PackProgress*)self;
	progress->size = size;
	progress->steps = 0;
	progress->next_step_threshold = size / 1000;
	progress->vtable.print(progress);
}

static void packprogress_update(void *self, int pos) {
	PackProgress *progress = (PackProgress*)self;
	if (pos < progress->next_step_threshold) return;
	while (pos >= progress->next_step_threshold) {
		progress->steps += 1;
		progress->next_step_threshold = (long long) progress->size * (progress->steps + 1) / 1000;
	}
	progress->vtable.rewind(progress);
	progress->vtable.print(progress);
}

static void packprogress_end(void *self) {
	PackProgress *progress = (PackProgress*)self;
	progress->vtable.rewind(progress);
	printf("\033[K");
	fflush(stdout);
}

static void packprogress_print(PackProgress *progress) {
	progress->textlength = printf("[%d.%d%%]", progress->steps / 10, progress->steps % 10);
	fflush(stdout);
}

static void packprogress_rewind(PackProgress *progress) {
	printf("\033[%dD", progress->textlength);
}

PackProgress* packprogress_new(void) {
	PackProgress *progress = malloc(sizeof(PackProgress));
	if (!progress) return NULL;
	
	progress->vtable.begin = (void (*)(void*, int))packprogress_begin;
	progress->vtable.update = (void (*)(void*, int))packprogress_update;
	progress->vtable.end = (void (*)(void*))packprogress_end;
	progress->vtable.print = (void (*)(void*))packprogress_print;
	progress->vtable.rewind = (void (*)(void*))packprogress_rewind;
	
	return progress;
}

void packprogress_free(PackProgress *progress) {
	if (progress) {
		free(progress);
	}
}

// NoProgress implementation
static void noprogress_begin(void *self, int size) {
	fflush(stdout);
}

static void noprogress_update(void *self, int pos) {
	// Do nothing
}

static void noprogress_end(void *self) {
	// Do nothing
}

NoProgress* noprogress_new(void) {
	NoProgress *progress = malloc(sizeof(NoProgress));
	if (!progress) return NULL;
	
	progress->vtable.begin = (void (*)(void*, int))noprogress_begin;
	progress->vtable.update = (void (*)(void*, int))noprogress_update;
	progress->vtable.end = (void (*)(void*))noprogress_end;
	
	return progress;
}

void noprogress_free(NoProgress *progress) {
	if (progress) {
		free(progress);
	}
}
