/**
 * @file minishrinkler.c
 * @brief Minishrinkler command-line tool
 * @author Giovanni Bajo <giovannibajo@gmail.com>
 * 
 * This is the command-line interface for the Minishrinkler compressor.
 * It uses the minishrinkler_compress() API to compress files.
 * 
 * Usage:
 *   ./minishrinkler <input_file> <output_file>
 */

#include "minishrinkler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

/**
 * @brief Print usage information
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [--window <size_kb>] <input_file> <output_file>\n", program_name);
    printf("MiniShrinkler - Embedded-friendly version of the Shrinkler compressor\n");
    printf("Outputs raw compressed data without header (compatible with -d option)\n");
    printf("\n");
    printf("Options:\n");
    printf("  --window <size_kb>  Set hash table size in kilobytes (default: 4)\n");
    printf("\n");
}

/**
 * @brief Read file into buffer
 */
static uint8_t* read_file(const char *filename, size_t *file_size) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open input file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size < 0) {
        fprintf(stderr, "Error: Cannot determine file size for '%s'\n", filename);
        fclose(file);
        return NULL;
    }
    
    if (size == 0) {
        fprintf(stderr, "Error: Input file '%s' is empty\n", filename);
        fclose(file);
        return NULL;
    }
    
    // Allocate buffer
    uint8_t *buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "Error: Cannot allocate memory for file '%s'\n", filename);
        fclose(file);
        return NULL;
    }
    
    // Read file
    size_t bytes_read = fread(buffer, 1, size, file);
    fclose(file);
    
    if (bytes_read != (size_t)size) {
        fprintf(stderr, "Error: Failed to read file '%s' (read %zu of %ld bytes)\n", 
                filename, bytes_read, size);
        free(buffer);
        return NULL;
    }
    
    *file_size = bytes_read;
    return buffer;
}

/**
 * @brief Write buffer to file
 */
static bool write_file(const char *filename, const uint8_t *data, size_t size) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Error: Cannot create output file '%s': %s\n", filename, strerror(errno));
        return false;
    }
    
    size_t bytes_written = fwrite(data, 1, size, file);
    fclose(file);
    
    if (bytes_written != size) {
        fprintf(stderr, "Error: Failed to write file '%s' (wrote %zu of %zu bytes)\n", 
                filename, bytes_written, size);
        return false;
    }
    
    return true;
}

/**
 * @brief Main function
 */
int main(int argc, char *argv[]) {
    const char *input_file = NULL;
    const char *output_file = NULL;
    size_t window_size_kb = 5;  // Default: 5 KB
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--window") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --window requires a size value\n");
                print_usage(argv[0]);
                return 1;
            }
            char *endptr;
            long size = strtol(argv[i + 1], &endptr, 10);
            if (*endptr != '\0' || size <= 0 || size > 1024) {
                fprintf(stderr, "Error: Invalid window size '%s'. Must be 1-1024 KB\n", argv[i + 1]);
                return 1;
            }
            window_size_kb = (size_t)size;
            i++;  // Skip the size value
        } else if (input_file == NULL) {
            input_file = argv[i];
        } else if (output_file == NULL) {
            output_file = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (input_file == NULL || output_file == NULL) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Read input file
    size_t input_size;
    uint8_t *input_data = read_file(input_file, &input_size);
    if (!input_data) {
        return 1;
    }
    
    printf("Compressing %zu bytes...\n", input_size);
    
    // Calculate output buffer size
    size_t output_capacity = minishrinkler_get_max_compressed_size(input_size);
    uint8_t *output_data = malloc(output_capacity);
    if (!output_data) {
        fprintf(stderr, "Error: Cannot allocate output buffer\n");
        free(input_data);
        return 1;
    }
    
    // Calculate work memory size in bytes
    size_t work_memory_size = window_size_kb * 1024;
    
    printf("Using hash table size: %zu KB (%zu bytes)\n", window_size_kb, work_memory_size);
    
    // Compress data
    int compressed_size = minishrinkler_compress(input_data, input_size, output_data, output_capacity, work_memory_size);
    
    if (compressed_size < 0) {
        switch (compressed_size) {
            case -1:
                fprintf(stderr, "Error: Output buffer too small\n");
                break;
            case -2:
                fprintf(stderr, "Error: Invalid input parameters\n");
                break;
            case -3:
                fprintf(stderr, "Error: Compression failed\n");
                break;
            default:
                fprintf(stderr, "Error: Unknown compression error (%d)\n", compressed_size);
                break;
        }
        free(input_data);
        free(output_data);
        return 1;
    }
    
    // Write output file
    if (!write_file(output_file, output_data, compressed_size)) {
        free(input_data);
        free(output_data);
        return 1;
    }
    
    // Print results
    printf("Compression completed:\n");
    printf("  Original size: %zu bytes\n", input_size);
    printf("  Compressed size: %d bytes\n", compressed_size);
    printf("  Compression ratio: %.2f%%\n", (float)compressed_size / input_size * 100);
    
    // Cleanup
    free(input_data);
    free(output_data);
    
    return 0;
}
