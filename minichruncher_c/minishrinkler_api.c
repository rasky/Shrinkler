/**
 * @file minishrinkler_api.c
 * @brief Minishrinkler compression API implementation
 * @author Giovanni Bajo <giovannibajo@gmail.com>
 * 
 * This file contains the core compression logic for Minishrinkler,
 * providing a buffer-to-buffer compression API without any file I/O.
 * The implementation is an exact copy of the working logic from minishrinkler.c
 */

#include "minishrinkler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

// Compile-time trace control
#define TRACE_SHRINKLER 0

#if TRACE_SHRINKLER
#define tracef(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define tracef(fmt, ...) ((void)0)
#endif

// Configuration - Embedded optimized
#define MAX_FILE_SIZE (1024*1024)  // 1MB max for embedded
#define MAX_MATCH_LENGTH 65535
#define MAX_OFFSET 65535
#define MIN_MATCH_LENGTH 3
#define WINDOW_SIZE 65536

// Context configuration (must match decompressor)
#define ADJUST_SHIFT 4
#define NUM_SINGLE_CONTEXTS 513
#define NUM_CONTEXT_GROUPS 4
#define CONTEXT_GROUP_SIZE 256
#define NUM_CONTEXTS (NUM_SINGLE_CONTEXTS + NUM_CONTEXT_GROUPS * CONTEXT_GROUP_SIZE)

// Context indices
#define CONTEXT_KIND 0
#define CONTEXT_REPEATED -1
#define CONTEXT_GROUP_LIT 0
#define CONTEXT_GROUP_OFFSET 2
#define CONTEXT_GROUP_LENGTH 3

#define HASH_SIZE 768

// Data structures - Embedded optimized
typedef struct {
    uint16_t contexts[NUM_CONTEXTS];
    uint8_t *output;
    uint16_t output_size;    
    uint16_t output_capacity;
    int16_t dest_bit;        
    uint32_t intervalsize;
    uint32_t intervalmin;
} shr_rangecoder_t;

typedef struct {
    uint16_t after_first;     
    uint16_t prev_was_ref;    
    uint16_t parity;          
    uint16_t last_offset;
} shr_lzstate_t;

typedef struct {
    uint16_t pos; 
    uint16_t next;
    uint8_t match_len;  // Cache for match length to improve early exit
    uint8_t quality;    // Match quality indicator (0-255)
} shr_hash_entry_t;

// Embedded memory optimization: no static allocations
// All memory will be allocated dynamically in a single buffer

// Memory layout structure for embedded allocation
typedef struct {
    int size_table[128];
    shr_hash_entry_t hash_table[HASH_SIZE];
    shr_rangecoder_t coder;
    shr_lzstate_t state;
    int size_table_init;
} shr_work_buffer_t;

// Utility functions
static int min(int a, int b) { return a < b ? a : b; }

// Initialize size table
static void init_size_table(shr_work_buffer_t *mem) {
    if (mem->size_table_init) return;
    
    for (int i = 0; i < 128; i++) {
        mem->size_table[i] = (int) floor(0.5 + (8.0 - log((double) (128 + i)) / log(2.0)) * (1 << 6));
    }
    mem->size_table_init = 1;
}

// Improved hash function for 3-byte sequences
static unsigned int hash3(const unsigned char *data) {
    // Better distribution than simple bit shifting
    return ((data[0] * 31 + data[1]) * 31 + data[2]) % HASH_SIZE;
}

// Initialize hash table
static void init_hash_table(shr_work_buffer_t *mem) {
    memset(mem->hash_table, 0, sizeof(mem->hash_table));
}

// Update hash table with new position and cache match information
static void update_hash(shr_work_buffer_t *mem, const unsigned char *data, int pos, int data_size) {
    if (pos + 2 >= data_size) return;
    
    unsigned int hash = hash3(&data[pos]);
    
    // Calculate match quality based on data characteristics
    uint8_t quality = 0;
    if (pos + 3 < data_size) {
        // Better quality heuristic: focus on compressible patterns
        if (data[pos] == data[pos + 1] && data[pos + 1] == data[pos + 2]) {
            quality = 255; // Excellent quality for repeated data (RLE)
        } else if (data[pos] != data[pos + 1] && data[pos + 1] != data[pos + 2] && data[pos] != data[pos + 2]) {
            quality = 64;  // Lower quality for varied data (harder to compress)
        } else {
            quality = 128; // Medium quality for mixed patterns
        }
    }
    
    // Update hash table with new entry
    mem->hash_table[hash].next = mem->hash_table[hash].pos;
    mem->hash_table[hash].pos = pos;
    mem->hash_table[hash].quality = quality;
    mem->hash_table[hash].match_len = 0; // Will be calculated during match finding
}

// Exact copy of original RangeCoder logic (adapted for static allocation)
static void range_coder_init(shr_rangecoder_t *coder, uint8_t *output, int capacity) {
    coder->output = output;
    coder->output_capacity = capacity;
    coder->output_size = 0;
    coder->dest_bit = -1; // Start at -1 like original
    coder->intervalsize = 0x8000;
    coder->intervalmin = 0;
    
    // Initialize contexts (must match decompressor)
    for (int i = 0; i < NUM_CONTEXTS; i++) {
        coder->contexts[i] = 0x8000;
    }
    
    // Initialize output buffer
    memset(output, 0, capacity);
    
    // Ensure first byte is initialized
    output[0] = 0;
}

// Forward declaration for tracing
static void range_coder_trace_state(shr_rangecoder_t *coder, const char *operation, int context, int bit, int size);

// Exact copy of original add_bit function (adapted for static allocation)
static void add_bit(shr_rangecoder_t *coder) {
    int pos = coder->dest_bit;
    int bytepos;
    int bitmask;
    
    tracef("    ADD_BIT: start pos=%d\n", pos);
    
    do {
        pos--;
        if (pos < 0) return;
        bytepos = pos >> 3;
        bitmask = 0x80 >> (pos & 7);
        
        if (bytepos >= coder->output_capacity) {
            fprintf(stderr, "Output buffer overflow\n");
            exit(1);
        }
        
        // Initialize byte to 0 if not already done
        if (bytepos >= coder->output_size) {
            coder->output[bytepos] = 0;
        }
        
        uint8_t old_byte = coder->output[bytepos];
        coder->output[bytepos] ^= bitmask;
        uint8_t new_byte = coder->output[bytepos];
        
        tracef("    ADD_BIT: pos=%d bytepos=%d bitmask=0x%02x old_byte=0x%02x new_byte=0x%02x\n", 
               pos, bytepos, bitmask, old_byte, new_byte);
    } while ((coder->output[bytepos] & bitmask) == 0);
    
    // Update maximum output size
    if (bytepos + 1 > coder->output_size) {
        coder->output_size = bytepos + 1;
        tracef("    ADD_BIT: updated output_size=%d\n", coder->output_size);
    }
}

// Exact copy of original rangecoder_code function
static int range_coder_code(shr_rangecoder_t *coder, shr_work_buffer_t *mem, int context_index, int bit) {
    // Handle special context indices
    if (context_index < 0) {
        // Special contexts like CONTEXT_REPEATED (-1) are handled differently
        // For now, just return 0 size for these
        return 0;
    }
    
    // Calculate size_before, handling the case where dest_bit is -1 (initial state)
    int size_before;
    if (coder->dest_bit < 0) {
        // Initial state: dest_bit is -1, so size is just the sizetable entry
        size_before = mem->size_table[(coder->intervalsize - 0x8000) >> 8];
    } else {
        size_before = (coder->dest_bit << 6) + mem->size_table[(coder->intervalsize - 0x8000) >> 8];  // BIT_PRECISION = 6
    }
    
    // Trace the input state
    range_coder_trace_state(coder, "CODE_START", context_index, bit, size_before);
    
    unsigned prob = coder->contexts[context_index];
    unsigned threshold = (coder->intervalsize * prob) >> 16;
    unsigned new_prob;
    
    if (!bit) {
        // Zero
        coder->intervalmin += threshold;
        if (coder->intervalmin & 0x10000) {
            add_bit(coder);
        }
        coder->intervalsize = coder->intervalsize - threshold;
        new_prob = prob - (prob >> ADJUST_SHIFT);
    } else {
        // One
        coder->intervalsize = threshold;
        new_prob = prob + (0xffff >> ADJUST_SHIFT) - (prob >> ADJUST_SHIFT);
    }
    
    coder->contexts[context_index] = new_prob;
    
    // Renormalization
    int renormalizations = 0;
    while (coder->intervalsize < 0x8000) {
        coder->dest_bit++;
        coder->intervalsize <<= 1;
        coder->intervalmin <<= 1;
        if (coder->intervalmin & 0x10000) {
            tracef("    RENORM: carry bit detected, calling add_bit\n");
            add_bit(coder);
        }
        renormalizations++;
    }
    coder->intervalmin &= 0xffff;
    
    if (renormalizations > 0) {
        tracef("    RENORM: %d renormalizations, final intervalmin=0x%04x intervalsize=0x%04x dest_bit=%d\n", 
               renormalizations, coder->intervalmin, coder->intervalsize, coder->dest_bit);
    }
    
    // Calculate size_after, handling the case where dest_bit is -1 (initial state)
    int size_after;
    if (coder->dest_bit < 0) {
        // Initial state: dest_bit is -1, so size is just the sizetable entry
        size_after = mem->size_table[(coder->intervalsize - 0x8000) >> 8];
    } else {
        size_after = (coder->dest_bit << 6) + mem->size_table[(coder->intervalsize - 0x8000) >> 8];  // BIT_PRECISION = 6
    }
    int size_diff = size_after - size_before;
    
    // Trace the output state
    range_coder_trace_state(coder, "CODE_END", context_index, bit, size_diff);
    
    return size_diff;
}

static void range_coder_trace_state(shr_rangecoder_t *coder, const char *operation, int context, int bit, int size) {
    tracef("RANGECODER: %s context=%d bit=%d size=%d intervalmin=0x%04x intervalsize=0x%04x dest_bit=%d\n",
           operation, context, bit, size, coder->intervalmin, coder->intervalsize, coder->dest_bit);
}

// Exact copy of original rangecoder_finish function
static void range_coder_finish(shr_rangecoder_t *coder) {
    // Trace the finish start
    tracef("RANGECODER: FINISH_START intervalmin=0x%04x intervalsize=0x%04x dest_bit=%d\n",
           coder->intervalmin, coder->intervalsize, coder->dest_bit);
    
    int intervalmax = coder->intervalmin + coder->intervalsize;
    int final_min = 0;
    int final_size = 0x10000;
    
    tracef("RANGECODER: FINISH_DETAILED_START intervalmin=0x%04x intervalsize=0x%04x dest_bit=%d intervalmax=0x%04x\n",
           coder->intervalmin, coder->intervalsize, coder->dest_bit, intervalmax);
    
    int finish_iterations = 0;
    while (final_min < coder->intervalmin || final_min + final_size >= intervalmax) {
        if (final_min + final_size < intervalmax) {
            tracef("RANGECODER: FINISH_ITERATION %d: final_min=0x%04x final_size=0x%04x < intervalmax=0x%04x -> add_bit\n",
                   finish_iterations, final_min, final_size, intervalmax);
            add_bit(coder);
            final_min += final_size;
        } else {
            tracef("RANGECODER: FINISH_ITERATION %d: final_min=0x%04x final_size=0x%04x >= intervalmax=0x%04x -> no add_bit\n",
                   finish_iterations, final_min, final_size, intervalmax);
        }
        coder->dest_bit++;
        final_size >>= 1;
        finish_iterations++;
    }
    
    int required_bytes = ((coder->dest_bit - 1) >> 3) + 1;
    if (required_bytes > coder->output_size) {
        coder->output_size = required_bytes;
    }
    
    // Trace the finish end
    tracef("RANGECODER: FINISH_END final dest_bit=%d out_size=%d required_bytes=%d\n",
           coder->dest_bit, coder->output_size, required_bytes);
}

// LZ Encoder
static void lz_state_init(shr_lzstate_t *state) {
    state->after_first = 0;
    state->prev_was_ref = 0;
    state->parity = 0;
    state->last_offset = 0;
}

static int encode_literal(shr_rangecoder_t *coder, shr_work_buffer_t *mem, unsigned char value, shr_lzstate_t *state) {
    int parity = state->parity & 1;
    int size = 0;
    
    // Trace literal encoding
    tracef("LZ: ENCODE_LITERAL value=0x%02x (%c) parity=%d after_first=%d\n", 
           value, (value >= 32 && value <= 126) ? value : '.', parity, state->after_first);
    
    if (state->after_first) {
        range_coder_code(coder, mem, 1 + CONTEXT_KIND + (parity << 8), 0); // KIND_LIT
        size++;
    }
    
    int context = 1;
    for (int i = 7; i >= 0; i--) {
        int bit = ((value >> i) & 1);
        int actual_context = 1 + ((parity << 8) | context);  // Correct context calculation
        range_coder_code(coder, mem, actual_context, bit);
        size++;
        context = (context << 1) | bit;
    }
    
    // Update state
    state->after_first = 1;
    state->prev_was_ref = 0;
    state->parity = state->parity + 1;
    
    return size;
}

// Exact copy of coder_encode_number from cruncher_c/Coder.c (adapted for RangeCoder)
static int encode_number(shr_rangecoder_t *coder, shr_work_buffer_t *mem, int context_group, int number) {
    int base_context = 1 + (context_group << 8);
    
    // Trace number encoding start
    tracef("ENCODE_NUMBER: group=%d number=%d base_context=%d\n", 
           context_group, number, base_context);
    
    // Must be >= 2 like original
    if (number < 2) {
        tracef("ERROR: number < 2 (%d)\n", number);
        return 0;
    }
    
    int size = 0;
    int context;
    int i;
    
    // First loop: continuation bits (exact copy)
    for (i = 0 ; (4 << i) <= number ; i++) {
        context = base_context + (i * 2 + 2);
        tracef("  CONTINUE_BIT: i=%d context=%d bit=1 (4<<i=%d <= %d)\n", 
               i, context, (4 << i), number);
        size += range_coder_code(coder, mem, context, 1);
    }
    
    // Stop bit (exact copy)
    context = base_context + (i * 2 + 2);
    tracef("  STOP_BIT: i=%d context=%d bit=0 (4<<i=%d > %d)\n", 
           i, context, (4 << i), number);
    size += range_coder_code(coder, mem, context, 0);
    
    // Second loop: actual bits (exact copy)
    for (; i >= 0 ; i--) {
        int bit = ((number >> i) & 1);
        context = base_context + (i * 2 + 1);
        tracef("  NUMBER_BIT: i=%d context=%d bit=%d (number>>%d&1)\n", i, context, bit, i);
        size += range_coder_code(coder, mem, context, bit);
    }
    
    tracef("ENCODE_NUMBER: COMPLETE size=%d\n", size);
    
    return size;
}

static int encode_reference(shr_rangecoder_t *coder, shr_work_buffer_t *mem, int offset, int length, shr_lzstate_t *state) {
    int parity = state->parity & 1;
    int size = 0;
    
    // Trace reference encoding
    tracef("=== ENCODE_REFERENCE START ===\n");
    tracef("LZ: ENCODE_REFERENCE offset=%d length=%d parity=%d prev_was_ref=%d last_offset=%d\n", 
           offset, length, parity, state->prev_was_ref, state->last_offset);
    tracef("KIND_REF: context=%d bit=1\n", 1 + CONTEXT_KIND + (parity << 8));
    
    range_coder_code(coder, mem, 1 + CONTEXT_KIND + (parity << 8), 1); // KIND_REF
    size++;
    
    if (!state->prev_was_ref) {
        int repeated = offset == state->last_offset;
        tracef("REPEATED: context=%d bit=%d (offset %s last_offset)\n", 
               1 + CONTEXT_REPEATED, repeated, repeated ? "==" : "!=");
        range_coder_code(coder, mem, 1 + CONTEXT_REPEATED, repeated);
        size++;
    }
    
    if (offset != state->last_offset) {
        tracef("ENCODE_OFFSET: offset=%d (encoded as %d)\n", offset, offset + 2);
        size += encode_number(coder, mem, CONTEXT_GROUP_OFFSET, offset + 2);
    } else {
        tracef("SKIP_OFFSET: offset=%d same as last_offset\n", offset);
    }
    
    tracef("ENCODE_LENGTH: length=%d\n", length);
    size += encode_number(coder, mem, CONTEXT_GROUP_LENGTH, length);
    
    // Update state
    state->after_first = 1;
    state->prev_was_ref = 1;
    state->parity = state->parity + length;
    state->last_offset = offset;
    
    tracef("=== ENCODE_REFERENCE END ===\n");
    tracef("STATE UPDATE: after_first=%d prev_was_ref=%d parity=%d last_offset=%d\n", 
           state->after_first, state->prev_was_ref, state->parity, state->last_offset);
    tracef("TOTAL SIZE: %d\n\n", size);
    
    return size;
}

// Intelligent Match Finder using enhanced hash table
static int find_match(shr_work_buffer_t *mem, const unsigned char *data, int data_size, int pos, 
                     int *best_offset, int *best_length) {
    *best_length = 0;
    *best_offset = 0;
    
    if (pos + 2 >= data_size) return 0;
    
    int max_len = min(MAX_MATCH_LENGTH, data_size - pos);
    int max_offset = min(pos, MAX_OFFSET);
    
    // Use hash table to find potential matches
    unsigned int hash = hash3(&data[pos]);
    int candidate_pos = mem->hash_table[hash].pos;
    int matches_checked = 0;
    
    // Adaptive max_matches based on data size and position
    int max_matches = (data_size < 1024) ? 24 : 64; // More candidates for better compression
    
    // Track best quality match for early exit optimization
    int best_quality = 0;
    
    while (candidate_pos > 0 && matches_checked < max_matches) {
        int offset = pos - candidate_pos;
        
        if (offset > max_offset) break;
        
        // Use cached match length if available and recent
        int match_len = 0;
        if (mem->hash_table[hash].match_len > 0 && 
            candidate_pos == mem->hash_table[hash].pos) {
            // Use cached length as starting point
            match_len = mem->hash_table[hash].match_len;
        }
        
        // Check if we have a match
        while (match_len < max_len && 
               pos + match_len < data_size && 
               candidate_pos + match_len < data_size &&
               data[pos + match_len] == data[candidate_pos + match_len]) {
            match_len++;
        }
        
        // Update cached match length
        if (candidate_pos == mem->hash_table[hash].pos) {
            mem->hash_table[hash].match_len = match_len;
        }
        
        // Calculate match quality score with encoding cost consideration
        int quality = mem->hash_table[hash].quality;
        if (match_len >= 8) quality += 128;  // Higher bonus for longer matches
        if (match_len >= 16) quality += 64;  // Extra bonus for very long matches
        if (offset <= 256) quality += 64;    // Higher bonus for closer matches
        if (offset <= 64) quality += 32;     // Extra bonus for very close matches
        
        // Consider encoding cost for better match selection
        int encoding_cost = 0;
        if (match_len >= 8) encoding_cost += 1;  // Extra bit for longer matches
        if (offset >= 256) encoding_cost += 1;   // Extra bit for larger offsets
        
        // Adjust quality based on encoding efficiency
        quality -= encoding_cost * 16;  // Penalize expensive encodings
        
        // Update if we found a better match (exclude offset 0)
        if (match_len >= MIN_MATCH_LENGTH && offset > 0) {
            // Enhanced match selection with pattern recognition
            int is_better = 0;
            
            // Primary criteria: length
            if (match_len > *best_length) {
                is_better = 1;
            } else if (match_len == *best_length) {
                // Secondary criteria: quality score
                if (quality > best_quality) {
                    is_better = 1;
                } else if (quality == best_quality) {
                    // Tertiary criteria: offset (prefer closer)
                    if (offset < *best_offset) {
                        is_better = 1;
                    }
                }
            }
            
            if (is_better) {
                *best_length = match_len;
                *best_offset = offset;
                best_quality = quality;
                
                // Early exit for excellent matches
                if (match_len >= 16 && quality >= 200) break;
            }
        }
        
        candidate_pos = mem->hash_table[hash].next;
        matches_checked++;
    }
    
    return *best_length >= MIN_MATCH_LENGTH;
}

// Main compression function
static int compress_data(const unsigned char *input, int input_size, 
                        unsigned char *output, int output_capacity) {
    int pos = 0;
    
    // Allocate embedded memory
    shr_work_buffer_t *mem = malloc(sizeof(shr_work_buffer_t));
    if (!mem) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return -4; // Memory allocation failed
    }
    
    // Initialize memory to zero
    memset(mem, 0, sizeof(shr_work_buffer_t));
    
    // Initialize
    init_size_table(mem);
    init_hash_table(mem);
    range_coder_init(&mem->coder, output, output_capacity);
    lz_state_init(&mem->state);
    
    // Enable tracing
    tracef("=== MINISHRINKLER TRACE ===\n");
    tracef("Input size: %d bytes\n", input_size);
    tracef("Input data: ");
    for (int i = 0; i < min(input_size, 32); i++) {
        tracef("%02x ", input[i]);
    }
    if (input_size > 32) tracef("...");
    tracef("\n\n");
    
    // Compression with lazy matching
    while (pos < input_size) {
        // Update hash table
        update_hash(mem, input, pos, input_size);
        
        int best_offset, best_length;
        
        if (find_match(mem, input, input_size, pos, &best_offset, &best_length)) {
            // Lazy matching: look ahead for better matches
            if (best_length >= 4 && pos + 1 < input_size) {
                int next_offset, next_length;
                
                // Update hash for next position
                update_hash(mem, input, pos + 1, input_size);
                
                if (find_match(mem, input, input_size, pos + 1, &next_offset, &next_length)) {
                    // Compare current match vs next position match
                    int current_cost = 2 + (best_length >= 8 ? 1 : 0) + (best_offset >= 256 ? 1 : 0);
                    int next_cost = 2 + (next_length >= 8 ? 1 : 0) + (next_offset >= 256 ? 1 : 0);
                    
                    // If next match is significantly better, encode literal and continue
                    if (next_length > best_length + 1 || 
                        (next_length == best_length + 1 && next_cost <= current_cost)) {
                        tracef("POS %d: LAZY LITERAL 0x%02x (waiting for better match)\n", pos, input[pos]);
                        encode_literal(&mem->coder, mem, input[pos], &mem->state);
                        pos++;
                        continue;
                    }
                }
            }
            
            // Encode reference
            tracef("POS %d: MATCH offset=%d length=%d\n", pos, best_offset, best_length);
            encode_reference(&mem->coder, mem, best_offset, best_length, &mem->state);
            pos += best_length;
        } else {
            // Encode literal
            tracef("POS %d: LITERAL 0x%02x (%c)\n", pos, input[pos], 
                   (input[pos] >= 32 && input[pos] <= 126) ? input[pos] : '.');
            encode_literal(&mem->coder, mem, input[pos], &mem->state);
            pos++;
        }
    }

    // Encode end marker (offset 0)
    tracef("END: ENCODE_END_MARKER\n");
    int parity = mem->state.parity & 1;
    range_coder_code(&mem->coder, mem, 1 + CONTEXT_KIND + (parity << 8), 1); // KIND_REF
    range_coder_code(&mem->coder, mem, 1 + CONTEXT_REPEATED, 0); // Not repeated
    encode_number(&mem->coder, mem, CONTEXT_GROUP_OFFSET, 2); // Offset 0 + 2 = 2 = end

    // Finalize
    range_coder_finish(&mem->coder);

    tracef("=== COMPRESSION COMPLETE ===\n");
    tracef("Final output size: %d bytes\n", mem->coder.output_size);
    
    // Save output size before freeing memory
    int output_size = mem->coder.output_size;
    
    // Free embedded memory
    free(mem);
    
    return output_size;
}

/**
 * @brief Get the maximum compressed size for given input size
 */
size_t minishrinkler_get_max_compressed_size(size_t input_size) {
    // Worst case: no compression + range coder overhead
    // Each byte might need up to 9 bits in worst case
    return (input_size * 9 + 7) / 8 + 64; // Add some safety margin
}

/**
 * @brief Compress data from input buffer to output buffer
 */
int minishrinkler_compress(
    const uint8_t *input_data,
    size_t input_size,
    uint8_t *output_buffer,
    size_t output_capacity
) {
    // Validate input parameters
    if (!input_data || !output_buffer || input_size == 0 || output_capacity == 0) {
        return -2; // Invalid parameters
    }
    
    // Check if output buffer is large enough
    size_t max_compressed_size = minishrinkler_get_max_compressed_size(input_size);
    if (output_capacity < max_compressed_size) {
        return -1; // Output buffer too small
    }
    
    // Check input size limit
    if (input_size > MAX_FILE_SIZE) {
        return -3; // Input too large
    }
    
    // Call the original compression function
    int result = compress_data((const unsigned char*)input_data, (int)input_size, 
                              (unsigned char*)output_buffer, (int)output_capacity);
    
    return result;
}
