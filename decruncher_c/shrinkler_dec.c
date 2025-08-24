/**
 * @file shrinkler_dec.c
 * @author Giovanni Bajo <giovannibajo@gmail.com>
 * @brief Shrinkler decompressor command line tool
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/// @cond
#if defined(__GNUC__) || defined(__clang__)
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

// Handle endianness - assume little endian for now
#define read32be(ptr) __builtin_bswap32(*(uint32_t*)(ptr))
/// @endcond

// Global trace flag
static bool g_trace = false;

#define ADJUST_SHIFT 4             ///< Shift amount for context probability adjustment

#define NUM_SINGLE_CONTEXTS 1      ///< Number of single contexts
#define NUM_CONTEXT_GROUPS 4       ///< Number of context groups  
#define CONTEXT_GROUP_SIZE 256     ///< Size of each context group
#define NUM_CONTEXTS  (NUM_SINGLE_CONTEXTS + NUM_CONTEXT_GROUPS * CONTEXT_GROUP_SIZE)  ///< Total number of contexts

#define CONTEXT_KIND 0             ///< Context kind index
#define CONTEXT_REPEATED -1        ///< Context repeated index

#define CONTEXT_GROUP_LIT 0        ///< Context group for literals
#define CONTEXT_GROUP_OFFSET 2     ///< Context group for offsets
#define CONTEXT_GROUP_LENGTH 3     ///< Context group for lengths

/** @brief Decompressor state (for assembly) */
typedef struct {
    uint16_t contexts[NUM_CONTEXTS];    ///< Probability contexts
} shrinkler_asm_state_t;

/** @brief Decompressor state (for C) */
typedef struct {
    uint16_t contexts[NUM_CONTEXTS];    ///< Probability contexts
    unsigned intervalsize;              ///< Current interval size
    uint64_t intervalvalue;             ///< Current interval value

    uint8_t *src;                       ///< Pointer to the input data
    uint8_t *src_end;                   ///< End of input data
    int bits_left;                      ///< Number of bits left in the interval
} shrinkler_ctx_t;

static void shr_decode_init(shrinkler_ctx_t *ctx, uint8_t *src, size_t src_size) {
    for (int i=0; i<NUM_CONTEXTS; i++)
        ctx->contexts[i] = 0x8000;
    
    ctx->intervalsize = 1;
    ctx->intervalvalue = 0;
    ctx->src = src;
    ctx->src_end = src + src_size;
    ctx->bits_left = 0;

    // Adjust for 64-bit values
    for (int i=0;i<4;i++) {
        if (ctx->src >= ctx->src_end) {
            fprintf(stderr, "ERROR: Not enough data for initialization\n");
            return;
        }
        ctx->intervalvalue = (ctx->intervalvalue << 8) | *ctx->src++;
    }
    ctx->intervalvalue <<= 31;
    ctx->bits_left = 1;
    ctx->intervalsize = 0x8000;
}

static inline int shr_decode_bit(shrinkler_ctx_t *ctx, int context_index) {
    if (context_index < 0 || context_index >= NUM_CONTEXTS) {
        fprintf(stderr, "ERROR: Invalid context index %d (valid range: 0-%d)\n", context_index, NUM_CONTEXTS-1);
        return -1;
    }
    
    if (g_trace) {
        fprintf(stderr, "      SHR_DECODE_BIT: context=%d intervalsize=0x%04x intervalvalue=0x%016llx bits_left=%d\n", 
               context_index, ctx->intervalsize, ctx->intervalvalue, ctx->bits_left);
    }
    
    while ((ctx->intervalsize < 0x8000)) {
        if (unlikely(ctx->bits_left == 0)) {
            // Check if we have enough data to read 4 bytes
            if (ctx->src + 4 > ctx->src_end) {
                fprintf(stderr, "ERROR: Unexpected end of compressed data\n");
                return -1;
            }
            ctx->intervalvalue |= read32be(ctx->src);
            ctx->src += 4;
            ctx->bits_left = 32;
            if (g_trace) {
                fprintf(stderr, "      RENORM: read32be, bits_left=32\n");
            }
        }
        ctx->bits_left -= 1;
        ctx->intervalsize <<= 1;
        ctx->intervalvalue <<= 1;
    }

    unsigned prob = ctx->contexts[context_index];
    unsigned intervalvalue = ctx->intervalvalue >> 48;
    unsigned threshold = (ctx->intervalsize * prob) >> 16;

    if (g_trace) {
        fprintf(stderr, "      DECODE: prob=0x%04x intervalvalue=0x%04x threshold=0x%04x\n", 
               prob, intervalvalue, threshold);
    }

    if (intervalvalue >= threshold) {
        // Zero
        ctx->intervalvalue -= (uint64_t)threshold << 48;
        ctx->intervalsize -= threshold;
        ctx->contexts[context_index] = prob - (prob >> ADJUST_SHIFT);
        if (g_trace) {
            fprintf(stderr, "      DECODE_ZERO: intervalvalue(0x%04x) >= threshold(0x%04x) -> bit=0\n", 
                   intervalvalue, threshold);
        }
        return 0;
    } else {
        // One
        ctx->intervalsize = threshold;
        ctx->contexts[context_index] = prob + (0xffff >> ADJUST_SHIFT) - (prob >> ADJUST_SHIFT);
        if (g_trace) {
            fprintf(stderr, "      DECODE_ONE: intervalvalue(0x%04x) < threshold(0x%04x) -> bit=1\n", 
                   intervalvalue, threshold);
        }
        return 1;
    }
}

static inline int shr_decode_number(shrinkler_ctx_t *ctx, int base_context) {
    int context;
    int i;
    
    if (g_trace) {
        fprintf(stderr, "    SHR_DECODE_NUMBER: base_context=%d\n", base_context);
    }
    
    // First loop: find number of bits
    for (i = 0 ; i < 16 ; i++) {  // Limit to 16 bits to prevent buffer overflow
        context = base_context + (i * 2 + 2);
        if (context >= NUM_CONTEXTS) {
            fprintf(stderr, "ERROR: Context index %d out of bounds (max %d)\n", context, NUM_CONTEXTS);
            return -1;
        }
        int continue_bit = shr_decode_bit(ctx, context);
        if (continue_bit == -1) {
            return -1;  // Error already reported
        }
        if (g_trace) {
            fprintf(stderr, "    CONTINUE_BIT: i=%d context=%d bit=%d (4<<i=%d)\n", 
                   i, context, continue_bit, (4 << i));
        }
        if (continue_bit == 0) break;
    }

    if (g_trace) {
        fprintf(stderr, "    STOP_BIT: i=%d (will decode %d bits)\n", i, i+1);
    }

    // Second loop: decode the actual number
    int number = 1;
    if (g_trace) {
        fprintf(stderr, "    NUMBER_START: number=1\n");
    }
    
    for (; i >= 0 ; i--) {
        context = base_context + (i * 2 + 1);
        if (context >= NUM_CONTEXTS) {
            fprintf(stderr, "ERROR: Context index %d out of bounds (max %d)\n", context, NUM_CONTEXTS);
            return -1;
        }
        int bit = shr_decode_bit(ctx, context);
        if (bit == -1) {
            return -1;  // Error already reported
        }
        int old_number = number;
        number = (number << 1) | bit;
        if (g_trace) {
            fprintf(stderr, "    NUMBER_BIT: i=%d context=%d bit=%d old_number=%d new_number=%d (%d<<1|%d)\n", 
                   i, context, bit, old_number, number, old_number, bit);
        }
    }

    if (g_trace) {
        fprintf(stderr, "    SHR_DECODE_NUMBER: RESULT=%d\n", number);
    }

    return number;
}

static inline int lzDecode(shrinkler_ctx_t *ctx, int context) {
    int result = shr_decode_bit(ctx, NUM_SINGLE_CONTEXTS + context);
    if (result == -1) {
        return -1;  // Error already reported
    }
    return result;
}

static inline int lzDecodeNumber(shrinkler_ctx_t *ctx, int context_group) {
    return shr_decode_number(ctx, NUM_SINGLE_CONTEXTS + (context_group << 8));
}

/**
 * @brief Increase output buffer size by doubling it
 */
static int increase_buffer(uint8_t **dst, uint8_t **dst_start, uint8_t **dst_end, size_t *dst_size) {
    size_t current_offset = *dst - *dst_start;
    size_t new_size = *dst_size * 2;
    
    uint8_t *new_dst = realloc(*dst_start, new_size);
    if (!new_dst) {
        return -1;
    }
    
    *dst_start = new_dst;
    *dst = new_dst + current_offset;
    *dst_end = new_dst + new_size;
    *dst_size = new_size;
        
    return 0;
}

static int shr_unpack(uint8_t **dst_start, uint8_t *src, size_t src_size)
{
    const int parity_mask = 1;

    // Allocate initial output buffer (same size as input)
    size_t dst_size = src_size;
    *dst_start = malloc(dst_size);
    if (!*dst_start) {
        return -1;
    }
    uint8_t *dst = *dst_start;
    uint8_t *dst_end = dst + dst_size;
        
    shrinkler_ctx_t ctx;
    shr_decode_init(&ctx, src, src_size);
    
    int ref = false;
    bool prev_was_ref = false;
    int offset = 0;

    if (g_trace) {
        fprintf(stderr, "=== SHRINKLER DECOMPRESSOR TRACE ===\n");
    }

    while (1) {
        if (ref) {
            int repeated = false;
            if (!prev_was_ref) {
                repeated = lzDecode(&ctx, CONTEXT_REPEATED);
                if (repeated == -1) {
                    return -1;  // Error already reported
                }
                if (g_trace) {
                    fprintf(stderr, "POS %ld: DECODE_REPEATED = %s\n", dst - *dst_start, repeated ? "true" : "false");
                }
            }
            if (!repeated) {
                int encoded_offset = lzDecodeNumber(&ctx, CONTEXT_GROUP_OFFSET);
                if (encoded_offset == -1) {
                    return -1;  // Error already reported
                }
                offset = encoded_offset - 2;
                if (g_trace) {
                    fprintf(stderr, "POS %ld: DECODE_OFFSET encoded=%d offset=%d\n", dst - *dst_start, encoded_offset, offset);
                }
                if (offset == 0) {
                    if (g_trace) {
                        fprintf(stderr, "POS %ld: END_MARKER detected\n", dst - *dst_start);
                    }
                    break;
                }
            }
            int length = lzDecodeNumber(&ctx, CONTEXT_GROUP_LENGTH);
            if (length == -1) {
                return -1;  // Error already reported
            }
            if (g_trace) {
                fprintf(stderr, "POS %ld: DECODE_LENGTH = %d\n", dst - *dst_start, length);
                fprintf(stderr, "POS %ld: MATCH offset=%d length=%d (copy from pos %ld)\n", 
                       dst - *dst_start, offset, length, (dst - *dst_start) - offset);
            }
            prev_was_ref = true;
            
            // Copy data
            int orig_length = length;
            if (offset > 8) 
                while (length >= 8) {
                    // Check if we need to expand the buffer
                    if (dst + 8 > dst_end) {
                        if (increase_buffer(&dst, dst_start, &dst_end, &dst_size) != 0) {
                            return -1;
                        }
                    }
                    memcpy(dst, dst - offset, 8);
                    dst += 8;
                    length -= 8;
                }            
            while (length--) {
                // Check if we need to expand the buffer
                if (dst >= dst_end) {
                    if (increase_buffer(&dst, dst_start, &dst_end, &dst_size) != 0) {
                        return -1;
                    }
                }
                *dst = dst[-offset];
                dst++;
            }
            
            if (g_trace) {
                fprintf(stderr, "POS %ld: MATCH_COMPLETE copied %d bytes to pos %ld\n", 
                       dst - *dst_start, orig_length, (dst - *dst_start) - orig_length);
            }
        } else {
            int parity = (dst - *dst_start) & parity_mask;
            int context = 1;
            for (int i = 7 ; i >= 0 ; i--) {
                int bit = lzDecode(&ctx, (parity << 8) | context);
                if (bit == -1) {
                    return -1;  // Error already reported
                }
                context = (context << 1) | bit;
            }
            uint8_t lit = context;
            if (g_trace) {
                fprintf(stderr, "POS %ld: LITERAL 0x%02x (%c) parity=%d\n", 
                       dst - *dst_start, lit, (lit >= 32 && lit <= 126) ? lit : '.', parity);
            }
            
            // Check if we need to expand the buffer
            if (dst >= dst_end) {
                if (increase_buffer(&dst, dst_start, &dst_end, &dst_size) != 0) {
                    return -1;
                }
            }
            
            *dst++ = lit;
            prev_was_ref = false;
        }
        int parity = (dst - *dst_start) & parity_mask;
        ref = lzDecode(&ctx, CONTEXT_KIND + (parity << 8));
        if (ref == -1) {
            return -1;  // Error already reported
        }
        if (g_trace) {
            fprintf(stderr, "POS %ld: DECODE_KIND = %s (parity=%d, context=%d)\n", 
                   dst - *dst_start, ref ? "REF" : "LIT", parity, CONTEXT_KIND + (parity << 8));
        }
    }
    
    return (int)(dst - *dst_start);
}

/**
 * @brief Decompress a Shrinkler-compressed buffer.
 *
 * @param src Source compressed data
 * @param src_size Size of compressed data
 * @param dst_ptr Pointer to destination buffer (will be allocated dynamically)
 * @return Size of decompressed data, or -1 on error
 */
int shrinkler_decompress(const uint8_t *src, size_t src_size, uint8_t **dst_ptr) {
    if (!src || !dst_ptr) {
        return -1;
    }
    
    // Create a copy of src since the algorithm modifies it
    uint8_t *src_copy = malloc(src_size);
    if (!src_copy) {
        return -1;
    }
    
    memcpy(src_copy, src, src_size);
    int dec_size = shr_unpack(dst_ptr, src_copy, src_size);
    free(src_copy);
    
    return dec_size;
}

/**
 * @brief Read file into buffer
 * 
 * The decompressor reads data in 4-byte chunks, so the input file size
 * must be padded to a multiple of 4 bytes. Any padding bytes are set to 0.
 */
uint8_t* read_file(const char *filename, size_t *size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    size_t original_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Round up to next multiple of 4 bytes for padding, plus add 4 bytes for the initial interval
    size_t padded_size = ((original_size + 3) & ~3) + 4;
    *size = padded_size;
    
    uint8_t *data = malloc(padded_size);
    if (!data) {
        fprintf(stderr, "Error: Cannot allocate memory for file\n");
        fclose(f);
        return NULL;
    }
    
    // Initialize padding bytes to 0
    memset(data, 0, padded_size);
    
    if (fread(data, 1, original_size, f) != original_size) {
        fprintf(stderr, "Error: Cannot read file '%s'\n", filename);
        free(data);
        fclose(f);
        return NULL;
    }
    
    fclose(f);
    return data;
}

/**
 * @brief Write buffer to file
 */
bool write_file(const char *filename, const uint8_t *data, size_t size) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error: Cannot create file '%s': %s\n", filename, strerror(errno));
        return false;
    }
    
    if (fwrite(data, 1, size, f) != size) {
        fprintf(stderr, "Error: Cannot write file '%s'\n", filename);
        fclose(f);
        return false;
    }
    
    fclose(f);
    return true;
}

void print_usage(const char *progname) {
    printf("Shrinkler Decompressor\n");
    printf("Usage: %s [options] <input_file> [output_file]\n", progname);
    printf("\nOptions:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --verbose  Verbose output\n");
    printf("  --trace        Enable decompression trace\n");
    printf("\nIf output_file is not specified, output goes to stdout\n");
    printf("\nExample:\n");
    printf("  %s compressed.shr decompressed.bin\n", progname);
    printf("  %s compressed.shr > decompressed.bin\n", progname);
}

int main(int argc, char *argv[]) {
    bool verbose = false;
    const char *input_file = NULL;
    const char *output_file = NULL;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--trace") == 0) {
            g_trace = true;
        } else if (!input_file) {
            input_file = argv[i];
        } else if (!output_file) {
            output_file = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (!input_file) {
        fprintf(stderr, "Error: Input file required\n");
        print_usage(argv[0]);
        return 1;
    }
    
    if (verbose) {
        printf("Decompressing '%s'...\n", input_file);
    }
    
    // Read input file
    size_t src_size;
    uint8_t *src_data = read_file(input_file, &src_size);
    if (!src_data) {
        return 1;
    }
    
    if (verbose) {
        printf("Compressed size: %zu bytes\n", src_size);
    }
    
    // Decompress (shr_unpack will allocate its own buffer dynamically)
    uint8_t *dst_data = NULL;
    int dec_size = shrinkler_decompress(src_data, src_size, &dst_data);
    if (dec_size < 0) {
        fprintf(stderr, "Error: Decompression failed: corrupted or invalid bitstream\n");
        free(src_data);
        return 1;
    }
    
    if (verbose) {
        printf("Decompressed size: %d bytes\n", dec_size);
        printf("Compression ratio: %.2f%%\n", (float)src_size / dec_size * 100);
    }
    
    // Write output
    bool success;
    if (output_file) {
        success = write_file(output_file, dst_data, dec_size);
        if (success && verbose) {
            printf("Output written to '%s'\n", output_file);
        }
    } else {
        // Write to stdout
        if (fwrite(dst_data, 1, (size_t)dec_size, stdout) != (size_t)dec_size) {
            fprintf(stderr, "Error: Cannot write to stdout\n");
            success = false;
        } else {
            success = true;
        }
    }
    
    // Cleanup
    free(src_data);
    free(dst_data);
    
    return success ? 0 : 1;
}
