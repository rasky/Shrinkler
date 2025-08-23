// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Progress reporting interface for LZ parsing.

*/

#pragma once

typedef struct {
	void (*begin)(void *self, int size);
	void (*update)(void *self, int pos);
	void (*end)(void *self);
	void (*print)(void *self);
	void (*rewind)(void *self);
} LZProgress;

// Progress implementations
typedef struct {
	LZProgress vtable;
	int size;
	int steps;
	int next_step_threshold;
	int textlength;
} PackProgress;

typedef struct {
	LZProgress vtable;
} NoProgress;

// Function declarations
PackProgress* packprogress_new(void);
void packprogress_free(PackProgress *progress);
NoProgress* noprogress_new(void);
void noprogress_free(NoProgress *progress);
