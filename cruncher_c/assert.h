// Copyright 1999-2022 Aske Simon Christensen. See LICENSE.txt for usage terms.

/*

Assertion macros.

*/

#pragma once

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define internal_error() do { \
    fprintf(stderr, "Internal error in %s:%d\n", __FILE__, __LINE__); \
    exit(1); \
} while(0)
