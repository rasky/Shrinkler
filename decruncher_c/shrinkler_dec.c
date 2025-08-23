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
    int bits_left;                      ///< Number of bits left in the interval
} shrinkler_ctx_t;

static void shr_decode_init(shrinkler_ctx_t *ctx, uint8_t *src) {
    for (int i=0; i<NUM_CONTEXTS; i++)
        ctx->contexts[i] = 0x8000;
    
    ctx->intervalsize = 1;
    ctx->intervalvalue = 0;
    ctx->src = src;
    ctx->bits_left = 0;

    // Adjust for 64-bit values
    for (int i=0;i<4;i++)
        ctx->intervalvalue = (ctx->intervalvalue << 8) | *ctx->src++;
    ctx->intervalvalue <<= 31;
    ctx->bits_left = 1;
    ctx->intervalsize = 0x8000;
}

static inline int shr_decode_bit(shrinkler_ctx_t *ctx, int context_index) {
    if (g_trace) {
        fprintf(stderr, "      SHR_DECODE_BIT: context=%d intervalsize=0x%04x intervalvalue=0x%016llx bits_left=%d\n", 
               context_index, ctx->intervalsize, ctx->intervalvalue, ctx->bits_left);
    }
    
    while ((ctx->intervalsize < 0x8000)) {
        if (unlikely(ctx->bits_left == 0)) {
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
    for (i = 0 ;; i++) {
        context = base_context + (i * 2 + 2);
        int continue_bit = shr_decode_bit(ctx, context);
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
        int bit = shr_decode_bit(ctx, context);
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
    return shr_decode_bit(ctx, NUM_SINGLE_CONTEXTS + context);
}

static inline int lzDecodeNumber(shrinkler_ctx_t *ctx, int context_group) {
    return shr_decode_number(ctx, NUM_SINGLE_CONTEXTS + (context_group << 8));
}

static int shr_unpack(uint8_t *dst, uint8_t *src)
{
    const int parity_mask = 1;

    uint8_t *dst_start = dst;
    shrinkler_ctx_t ctx;
    shr_decode_init(&ctx, src);

    bool ref = false;
    bool prev_was_ref = false;
    int offset = 0;

    if (g_trace) {
        fprintf(stderr, "=== SHRINKLER DECOMPRESSOR TRACE ===\n");
    }

    while (1) {
        if (ref) {
            bool repeated = false;
            if (!prev_was_ref) {
                repeated = lzDecode(&ctx, CONTEXT_REPEATED);
                if (g_trace) {
                    fprintf(stderr, "POS %ld: DECODE_REPEATED = %s\n", dst - dst_start, repeated ? "true" : "false");
                }
            }
            if (!repeated) {
                int encoded_offset = lzDecodeNumber(&ctx, CONTEXT_GROUP_OFFSET);
                offset = encoded_offset - 2;
                if (g_trace) {
                    fprintf(stderr, "POS %ld: DECODE_OFFSET encoded=%d offset=%d\n", dst - dst_start, encoded_offset, offset);
                }
                if (offset == 0) {
                    if (g_trace) {
                        fprintf(stderr, "POS %ld: END_MARKER detected\n", dst - dst_start);
                    }
                    break;
                }
            }
            int length = lzDecodeNumber(&ctx, CONTEXT_GROUP_LENGTH);
            if (g_trace) {
                fprintf(stderr, "POS %ld: DECODE_LENGTH = %d\n", dst - dst_start, length);
                fprintf(stderr, "POS %ld: MATCH offset=%d length=%d (copy from pos %ld)\n", 
                       dst - dst_start, offset, length, (dst - dst_start) - offset);
            }
            prev_was_ref = true;
            
            // Copy data
            int orig_length = length;
            if (offset > 8) 
                while (length >= 8) {
                    memcpy(dst, dst - offset, 8);
                    dst += 8;
                    length -= 8;
                }            
            while (length--) {
                *dst = dst[-offset];
                dst++;
            }
            
            if (g_trace) {
                fprintf(stderr, "POS %ld: MATCH_COMPLETE copied %d bytes to pos %ld\n", 
                       dst - dst_start, orig_length, (dst - dst_start) - orig_length);
            }
        } else {
            int parity = (dst - dst_start) & parity_mask;
            int context = 1;
            for (int i = 7 ; i >= 0 ; i--) {
                int bit = lzDecode(&ctx, (parity << 8) | context);
                context = (context << 1) | bit;
            }
            uint8_t lit = context;
            if (g_trace) {
                fprintf(stderr, "POS %ld: LITERAL 0x%02x (%c) parity=%d\n", 
                       dst - dst_start, lit, (lit >= 32 && lit <= 126) ? lit : '.', parity);
            }
            *dst++ = lit;
            prev_was_ref = false;
        }
        int parity = (dst - dst_start) & parity_mask;
        ref = lzDecode(&ctx, CONTEXT_KIND + (parity << 8));
        if (g_trace) {
            fprintf(stderr, "POS %ld: DECODE_KIND = %s (parity=%d, context=%d)\n", 
                   dst - dst_start, ref ? "REF" : "LIT", parity, CONTEXT_KIND + (parity << 8));
        }
    }
    
    return dst - dst_start;
}

/**
 * @brief Decompress a Shrinkler-compressed buffer.
 *
 * @param src Source compressed data
 * @param src_size Size of compressed data
 * @param dst Destination buffer (must be large enough)
 * @return Size of decompressed data, or -1 on error
 */
int shrinkler_decompress(const uint8_t *src, size_t src_size, uint8_t *dst) {
    if (!src || !dst) {
        return -1;
    }
    
    // Create a copy of src since the algorithm modifies it
    uint8_t *src_copy = malloc(src_size);
    if (!src_copy) {
        return -1;
    }
    
    memcpy(src_copy, src, src_size);
    int dec_size = shr_unpack(dst, src_copy);
    free(src_copy);
    
    return dec_size;
}

/**
 * @brief Read file into buffer
 */
uint8_t* read_file(const char *filename, size_t *size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *data = malloc(*size);
    if (!data) {
        fprintf(stderr, "Error: Cannot allocate memory for file\n");
        fclose(f);
        return NULL;
    }
    
    if (fread(data, 1, *size, f) != *size) {
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
    
    // Allocate output buffer (estimate size as 10x compressed size)
    size_t dst_size = src_size * 10;
    uint8_t *dst_data = malloc(dst_size);
    if (!dst_data) {
        fprintf(stderr, "Error: Cannot allocate output buffer\n");
        free(src_data);
        return 1;
    }
    
    // Decompress
    int dec_size = shrinkler_decompress(src_data, src_size, dst_data);
    if (dec_size < 0) {
        fprintf(stderr, "Error: Decompression failed\n");
        free(src_data);
        free(dst_data);
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
        if (fwrite(dst_data, 1, dec_size, stdout) != dec_size) {
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
