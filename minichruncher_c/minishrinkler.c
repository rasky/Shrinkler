/*
 * MiniShrinkler - Improved version of the Shrinkler compressor
 * 
 * Features:
 * - Compatible with existing Shrinkler decompressors
 * - No dynamic memory allocation during compression
 * - Compact code (~1000 lines)
 * - Improved match finding with hash table
 * - All text in English
 * 
 * Copyright 2024 - Based on Shrinkler by Aske Simon Christensen
 */

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

// Configuration
#define MAX_FILE_SIZE (1024*1024*100)  // 100MB max
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

#define HASH_SIZE 65536
#define HASH_MASK (HASH_SIZE - 1)

// Data structures
typedef struct {
    uint16_t contexts[NUM_CONTEXTS];
    uint8_t *output;
    int output_size;
    int output_capacity;
    int dest_bit;
    uint32_t intervalsize;
    uint32_t intervalmin;
} RangeCoder;

typedef struct {
    int after_first;
    int prev_was_ref;
    int parity;
    int last_offset;
} LZState;

typedef struct {
    int pos;
    int next;
} HashEntry;

// Precomputed tables
static int size_table[128];
static int size_table_init = 0;

// Hash table for match finding
static HashEntry hash_table[HASH_SIZE];

// Utility functions
static int min(int a, int b) { return a < b ? a : b; }

// Initialize size table
static void init_size_table() {
    if (size_table_init) return;
    
    for (int i = 0; i < 128; i++) {
        size_table[i] = (int) floor(0.5 + (8.0 - log((double) (128 + i)) / log(2.0)) * (1 << 6));
    }
    size_table_init = 1;
}

// Hash function for 3-byte sequences
static unsigned int hash3(const unsigned char *data) {
    return ((data[0] << 16) | (data[1] << 8) | data[2]) & HASH_MASK;
}

// Initialize hash table
static void init_hash_table() {
    memset(hash_table, 0, sizeof(hash_table));
}

// Update hash table with new position
static void update_hash(const unsigned char *data, int pos, int data_size) {
    if (pos + 2 >= data_size) return;
    
    unsigned int hash = hash3(&data[pos]);
    hash_table[hash].next = hash_table[hash].pos;
    hash_table[hash].pos = pos;
}

// Exact copy of original RangeCoder logic (adapted for static allocation)
static void range_coder_init(RangeCoder *coder, uint8_t *output, int capacity) {
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
static void range_coder_trace_state(RangeCoder *coder, const char *operation, int context, int bit, int size);

// Exact copy of original add_bit function (adapted for static allocation)
static void add_bit(RangeCoder *coder) {
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
static int range_coder_code(RangeCoder *coder, int context_index, int bit) {
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
        size_before = size_table[(coder->intervalsize - 0x8000) >> 8];
    } else {
        size_before = (coder->dest_bit << 6) + size_table[(coder->intervalsize - 0x8000) >> 8];  // BIT_PRECISION = 6
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
        size_after = size_table[(coder->intervalsize - 0x8000) >> 8];
    } else {
        size_after = (coder->dest_bit << 6) + size_table[(coder->intervalsize - 0x8000) >> 8];  // BIT_PRECISION = 6
    }
    int size_diff = size_after - size_before;
    
    // Trace the output state
    range_coder_trace_state(coder, "CODE_END", context_index, bit, size_diff);
    
    return size_diff;
}



static void range_coder_trace_state(RangeCoder *coder, const char *operation, int context, int bit, int size) {
    tracef("RANGECODER: %s context=%d bit=%d size=%d intervalmin=0x%04x intervalsize=0x%04x dest_bit=%d\n",
           operation, context, bit, size, coder->intervalmin, coder->intervalsize, coder->dest_bit);
}

// Exact copy of original rangecoder_finish function
static void range_coder_finish(RangeCoder *coder) {
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
static void lz_state_init(LZState *state) {
    state->after_first = 0;
    state->prev_was_ref = 0;
    state->parity = 0;
    state->last_offset = 0;
}

static int encode_literal(RangeCoder *coder, unsigned char value, LZState *state) {
    int parity = state->parity & 1;
    int size = 0;
    
    // Trace literal encoding
    tracef("LZ: ENCODE_LITERAL value=0x%02x (%c) parity=%d after_first=%d\n", 
           value, (value >= 32 && value <= 126) ? value : '.', parity, state->after_first);
    
    if (state->after_first) {
        range_coder_code(coder, 1 + CONTEXT_KIND + (parity << 8), 0); // KIND_LIT
        size++;
    }
    
    int context = 1;
    for (int i = 7; i >= 0; i--) {
        int bit = ((value >> i) & 1);
        int actual_context = 1 + ((parity << 8) | context);  // Correct context calculation
        range_coder_code(coder, actual_context, bit);
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
static int encode_number(RangeCoder *coder, int context_group, int number) {
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
        size += range_coder_code(coder, context, 1);
    }
    
    // Stop bit (exact copy)
    context = base_context + (i * 2 + 2);
    tracef("  STOP_BIT: i=%d context=%d bit=0 (4<<i=%d > %d)\n", 
           i, context, (4 << i), number);
    size += range_coder_code(coder, context, 0);
    
    // Second loop: actual bits (exact copy)
    for (; i >= 0 ; i--) {
        int bit = ((number >> i) & 1);
        context = base_context + (i * 2 + 1);
        tracef("  NUMBER_BIT: i=%d context=%d bit=%d (number>>%d&1)\n", i, context, bit, i);
        size += range_coder_code(coder, context, bit);
    }
    
    tracef("ENCODE_NUMBER: COMPLETE size=%d\n", size);
    
    return size;
}

static int encode_reference(RangeCoder *coder, int offset, int length, LZState *state) {
    int parity = state->parity & 1;
    int size = 0;
    
    // Trace reference encoding
    tracef("=== ENCODE_REFERENCE START ===\n");
    tracef("LZ: ENCODE_REFERENCE offset=%d length=%d parity=%d prev_was_ref=%d last_offset=%d\n", 
           offset, length, parity, state->prev_was_ref, state->last_offset);
    tracef("KIND_REF: context=%d bit=1\n", 1 + CONTEXT_KIND + (parity << 8));
    
    range_coder_code(coder, 1 + CONTEXT_KIND + (parity << 8), 1); // KIND_REF
    size++;
    
        if (!state->prev_was_ref) {
        int repeated = offset == state->last_offset;
        tracef("REPEATED: context=%d bit=%d (offset %s last_offset)\n", 
               1 + CONTEXT_REPEATED, repeated, repeated ? "==" : "!=");
        range_coder_code(coder, 1 + CONTEXT_REPEATED, repeated);
        size++;
    }
    
    if (offset != state->last_offset) {
        tracef("ENCODE_OFFSET: offset=%d (encoded as %d)\n", offset, offset + 2);
        size += encode_number(coder, CONTEXT_GROUP_OFFSET, offset + 2);
    } else {
        tracef("SKIP_OFFSET: offset=%d same as last_offset\n", offset);
    }
    
    tracef("ENCODE_LENGTH: length=%d\n", length);
    size += encode_number(coder, CONTEXT_GROUP_LENGTH, length);
    
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

// Improved Match Finder using hash table
static int find_match(const unsigned char *data, int data_size, int pos, 
                     int *best_offset, int *best_length) {
    *best_length = 0;
    *best_offset = 0;
    
    if (pos + 2 >= data_size) return 0;
    
    int max_len = min(MAX_MATCH_LENGTH, data_size - pos);
    int max_offset = min(pos, MAX_OFFSET);
    
    // Use hash table to find potential matches
    unsigned int hash = hash3(&data[pos]);
    int candidate_pos = hash_table[hash].pos;
    int matches_checked = 0;
    const int max_matches = 32; // Limit number of candidates to check
    
    while (candidate_pos > 0 && matches_checked < max_matches) {
        int offset = pos - candidate_pos;
        
        if (offset > max_offset) break;
        
        // Check if we have a match
        int match_len = 0;
        while (match_len < max_len && 
               pos + match_len < data_size && 
               candidate_pos + match_len < data_size &&
               data[pos + match_len] == data[candidate_pos + match_len]) {
            match_len++;
        }
        
        // Update if we found a better match (exclude offset 0)
        if (match_len >= MIN_MATCH_LENGTH && match_len > *best_length && offset > 0) {
            *best_length = match_len;
            *best_offset = offset;
            
            // Early exit for very good matches
            if (match_len >= 16) break;
        }
        
        candidate_pos = hash_table[hash].next;
        matches_checked++;
    }
    
    return *best_length >= MIN_MATCH_LENGTH;
}

// Main compression function
static int compress_data(const unsigned char *input, int input_size, 
                        unsigned char *output, int output_capacity) {
    RangeCoder coder;
    LZState state;
    int pos = 0;
    
    // Initialize
    init_size_table();
    init_hash_table();
    range_coder_init(&coder, output, output_capacity);
    lz_state_init(&state);
    
    // Enable tracing
    tracef("=== MINISHRINKLER TRACE ===\n");
    tracef("Input size: %d bytes\n", input_size);
    tracef("Input data: ");
    for (int i = 0; i < min(input_size, 32); i++) {
        tracef("%02x ", input[i]);
    }
    if (input_size > 32) tracef("...");
    tracef("\n\n");
    
    // Compression
    while (pos < input_size) {
        // Update hash table
        update_hash(input, pos, input_size);
        
        int best_offset, best_length;
        
        if (find_match(input, input_size, pos, &best_offset, &best_length)) {
            // Encode reference
            tracef("POS %d: MATCH offset=%d length=%d\n", pos, best_offset, best_length);
            encode_reference(&coder, best_offset, best_length, &state);
            pos += best_length;
        } else {
            // Encode literal
            tracef("POS %d: LITERAL 0x%02x (%c)\n", pos, input[pos], 
                   (input[pos] >= 32 && input[pos] <= 126) ? input[pos] : '.');
            encode_literal(&coder, input[pos], &state);
            pos++;
        }
    }

    // Encode end marker (offset 0)
    tracef("END: ENCODE_END_MARKER\n");
    int parity = state.parity & 1;
    range_coder_code(&coder, 1 + CONTEXT_KIND + (parity << 8), 1); // KIND_REF
    range_coder_code(&coder, 1 + CONTEXT_REPEATED, 0); // Not repeated
    encode_number(&coder, CONTEXT_GROUP_OFFSET, 2); // Offset 0 + 2 = 2 = end

    // Finalize
    range_coder_finish(&coder);

    tracef("=== COMPRESSION COMPLETE ===\n");
    tracef("Final output size: %d bytes\n", coder.output_size);
    
    return coder.output_size;
}



// Main function
int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <input_file> <output_file>\n", argv[0]);
        printf("MiniShrinkler - Improved version of the Shrinkler compressor\n");
        printf("Outputs raw compressed data without header (compatible with -d option)\n");
        return 1;
    }
    
    const char *input_file = argv[1];
    const char *output_file = argv[2];
    
    // Read input file
    FILE *fin = fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "Error: cannot open input file %s\n", input_file);
        return 1;
    }
    
    // Get file size
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    if (file_size > MAX_FILE_SIZE) {
        fprintf(stderr, "Error: file too large (max %d bytes)\n", MAX_FILE_SIZE);
        fclose(fin);
        return 1;
    }
    
    // Allocate input buffer
    unsigned char *input_data = malloc(file_size);
    if (!input_data) {
        fprintf(stderr, "Error: cannot allocate memory for input data\n");
        fclose(fin);
        return 1;
    }
    
    // Read data
    if (fread(input_data, 1, file_size, fin) != file_size) {
        fprintf(stderr, "Error: cannot read input file\n");
        free(input_data);
        fclose(fin);
        return 1;
    }
    fclose(fin);
    
    // Allocate output buffer (worst case: input_size + overhead)
    int output_capacity = file_size + 1024; // Safety margin
    unsigned char *output_data = malloc(output_capacity);
    if (!output_data) {
        fprintf(stderr, "Error: cannot allocate memory for output data\n");
        free(input_data);
        return 1;
    }
    
    // Compress
    printf("Compressing %ld bytes...\n", file_size);
    int compressed_size = compress_data(input_data, file_size, output_data, output_capacity);
    
    // Write output file
    FILE *fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error: cannot create output file %s\n", output_file);
        free(input_data);
        free(output_data);
        return 1;
    }
    
    if (fwrite(output_data, 1, compressed_size, fout) != compressed_size) {
        fprintf(stderr, "Error: cannot write output file\n");
        fclose(fout);
        free(input_data);
        free(output_data);
        return 1;
    }
    fclose(fout);
    
    // Statistics
    printf("Compression completed:\n");
    printf("  Original size: %ld bytes\n", file_size);
    printf("  Compressed size: %d bytes\n", compressed_size);
    printf("  Compression ratio: %.2f%%\n", (float)compressed_size * 100 / file_size);
    
    // Cleanup
    free(input_data);
    free(output_data);
    
    return 0;
}
