#!/bin/bash

# Test script to verify compatibility between C, C++ and Mini versions of the Shrinkler compressor
# Author: Giovanni Bajo <giovannibajo@gmail.com>

set -e  # Exit on any error

# Configuration
TEST_C_VERSION=${TEST_C_VERSION:-false}  # Set to true to enable C version testing

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Utility functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_mini() {
    echo -e "${CYAN}[MINI]${NC} $1"
}

# Working directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR_C="build/native_c"
BUILD_DIR_CPP="build/native"
MINI_DIR="minichruncher_c"
TEST_DIR="testfiles"
OUTPUT_DIR="test_output"

# Executable names
C_EXECUTABLE="CShrinkler"
CPP_EXECUTABLE="Shrinkler"
MINI_EXECUTABLE="minishrinkler"

# Global statistics
TOTAL_FILES=0
PASSED_FILES=0
FAILED_FILES=0
TOTAL_ORIGINAL_SIZE=0
TOTAL_CPP_SIZE=0
TOTAL_C_SIZE=0
TOTAL_MINI_SIZE=0
TOTAL_GZIP_SIZE=0

# Directory statistics (using simple variables)
NOTES_FILES=0
NOTES_ORIGINAL_SIZE=0
NOTES_CPP_SIZE=0
NOTES_MINI_SIZE=0
NOTES_GZIP_SIZE=0

SPRITES_FILES=0
SPRITES_ORIGINAL_SIZE=0
SPRITES_CPP_SIZE=0
SPRITES_MINI_SIZE=0
SPRITES_GZIP_SIZE=0

# Function to clean temporary files
cleanup() {
    rm -rf "$OUTPUT_DIR" 2>/dev/null || true
}

# Function to handle exit
exit_handler() {
    cleanup
    echo
    print_final_report
}

# Set trap for cleanup
trap exit_handler EXIT INT TERM

# Function to print final report
print_final_report() {
    log_info "=== FINAL REPORT ==="
    log_info "Total tests: $TOTAL_FILES"
    log_success "Passed tests: $PASSED_FILES"
    if [ $FAILED_FILES -gt 0 ]; then
        log_error "Failed tests: $FAILED_FILES"
    fi
    
    if [ $TOTAL_ORIGINAL_SIZE -gt 0 ]; then
        echo
        log_info "=== COMPRESSION RATIO SUMMARY ==="
        
        # Calculate weighted averages
        local cpp_weighted_ratio=$(echo "scale=2; $TOTAL_CPP_SIZE * 100 / $TOTAL_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
        local mini_weighted_ratio=$(echo "scale=2; $TOTAL_MINI_SIZE * 100 / $TOTAL_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
        local gzip_weighted_ratio=$(echo "scale=2; $TOTAL_GZIP_SIZE * 100 / $TOTAL_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
        
        printf "%-20s %10s %8s %8s %8s\n" "Directory" "Files" "Shrinkler" "Mini" "Gzip"
        printf "%-20s %10s %8s %8s %8s\n" "--------------------" "----------" "--------" "--------" "--------"
        
        # Print notes directory stats
        if [ $NOTES_ORIGINAL_SIZE -gt 0 ]; then
            local notes_cpp_ratio=$(echo "scale=1; $NOTES_CPP_SIZE * 100 / $NOTES_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
            local notes_mini_ratio=$(echo "scale=1; $NOTES_MINI_SIZE * 100 / $NOTES_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
            local notes_gzip_ratio=$(echo "scale=1; $NOTES_GZIP_SIZE * 100 / $NOTES_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
            printf "%-20s %10s %8s %8s %8s\n" "notes" "$NOTES_FILES" "${notes_cpp_ratio}%" "${notes_mini_ratio}%" "${notes_gzip_ratio}%"
        fi
        
        # Print sprites directory stats
        if [ $SPRITES_ORIGINAL_SIZE -gt 0 ]; then
            local sprites_cpp_ratio=$(echo "scale=1; $SPRITES_CPP_SIZE * 100 / $SPRITES_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
            local sprites_mini_ratio=$(echo "scale=1; $SPRITES_MINI_SIZE * 100 / $SPRITES_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
            local sprites_gzip_ratio=$(echo "scale=1; $SPRITES_GZIP_SIZE * 100 / $SPRITES_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
            printf "%-20s %10s %8s %8s %8s\n" "sprites" "$SPRITES_FILES" "${sprites_cpp_ratio}%" "${sprites_mini_ratio}%" "${sprites_gzip_ratio}%"
        fi
        
        printf "%-20s %10s %8s %8s %8s\n" "--------------------" "----------" "--------" "--------" "--------"
        printf "%-20s %10s %8s %8s %8s\n" "WEIGHTED AVERAGE" "$TOTAL_FILES" "${cpp_weighted_ratio}%" "${mini_weighted_ratio}%" "${gzip_weighted_ratio}%"
        
        echo
        log_info "Performance comparison (weighted average):"
        if [ "$mini_weighted_ratio" != "N/A" ] && [ "$cpp_weighted_ratio" != "N/A" ]; then
            if (( $(echo "$mini_weighted_ratio < $cpp_weighted_ratio" | bc -l) )); then
                local diff=$(echo "scale=2; $cpp_weighted_ratio - $mini_weighted_ratio" | bc -l)
                log_success "✓ Minishrinkler beats Shrinkler by ${diff}%"
            else
                local diff=$(echo "scale=2; $mini_weighted_ratio - $cpp_weighted_ratio" | bc -l)
                log_warning "⚠ Minishrinkler is ${diff}% worse than Shrinkler"
            fi
        fi
        
        if [ "$mini_weighted_ratio" != "N/A" ] && [ "$gzip_weighted_ratio" != "N/A" ]; then
            if (( $(echo "$mini_weighted_ratio < $gzip_weighted_ratio" | bc -l) )); then
                local diff=$(echo "scale=2; $gzip_weighted_ratio - $mini_weighted_ratio" | bc -l)
                log_success "✓ Minishrinkler beats Gzip by ${diff}%"
            else
                local diff=$(echo "scale=2; $mini_weighted_ratio - $gzip_weighted_ratio" | bc -l)
                log_warning "⚠ Minishrinkler is ${diff}% worse than Gzip"
            fi
        fi
    fi
    
    if [ $FAILED_FILES -gt 0 ]; then
        exit 1
    else
        log_success "All tests passed!"
        exit 0
    fi
}

# Function to compile versions
compile_versions() {
    log_info "Compiling all versions..."
    if ! make all; then
        log_error "Compilation failed"
        exit 1
    fi
    
    # Verify that executables exist
    if [ ! -f "$BUILD_DIR_CPP/$CPP_EXECUTABLE" ]; then
        log_error "C++ executable not found: $BUILD_DIR_CPP/$CPP_EXECUTABLE"
        exit 1
    fi
    
    if [ "$TEST_C_VERSION" = "true" ] && [ ! -f "$BUILD_DIR_C/$C_EXECUTABLE" ]; then
        log_error "C executable not found: $BUILD_DIR_C/$C_EXECUTABLE"
        exit 1
    fi
    
    if [ ! -f "$MINI_DIR/$MINI_EXECUTABLE" ]; then
        log_error "Minishrinkler executable not found: $MINI_DIR/$MINI_EXECUTABLE"
        exit 1
    fi
    
    if [ ! -f "decruncher_c/shrinkler_dec" ]; then
        log_error "Decompressor executable not found: decruncher_c/shrinkler_dec"
        exit 1
    fi
    
    log_success "Compilation completed successfully"
}

# Function to check testsuite
check_testsuite() {
    if [ ! -d "$TEST_DIR" ]; then
        log_error "Test directory not found: $TEST_DIR"
        log_info "Please create the testfiles directory with your test files"
        exit 1
    fi
    
    local total_files=0
    for subdir in "$TEST_DIR"/*/; do
        if [ -d "$subdir" ]; then
            local dir_name=$(basename "$subdir")
            local file_count=$(find "$subdir" -type f | wc -l)
            total_files=$((total_files + file_count))
            log_info "Found $file_count files in $dir_name/"
        fi
    done
    
    if [ $total_files -eq 0 ]; then
        log_error "No test files found in subdirectories of $TEST_DIR"
        exit 1
    fi
    
    log_info "Found $total_files total test files"
}

# Function to calculate compression ratio
calculate_ratio() {
    local original_size="$1"
    local compressed_size="$2"
    echo "scale=2; $compressed_size * 100 / $original_size" | bc -l 2>/dev/null || echo "0"
}

# Function to test decompression
test_decompression() {
    local compressed_file="$1"
    local original_file="$2"
    local compressor_name="$3"
    
    # Decompress using our decompressor
    local decompressed_output="$OUTPUT_DIR/$(basename "$original_file").${compressor_name}.decompressed"
    if ! "./decruncher_c/shrinkler_dec" "$compressed_file" > "$decompressed_output" 2>/dev/null; then
        return 1
    fi
    
    # Compare with original
    if cmp -s "$original_file" "$decompressed_output"; then
        return 0
    else
        return 1
    fi
}

# Function to test a single file
test_file() {
    local input_file="$1"
    local filename=$(basename "$input_file")
    local dir_name=$(basename "$(dirname "$input_file")")
    local original_size=$(wc -c < "$input_file")
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Compress with C++ version
    local cpp_output="$OUTPUT_DIR/${filename}.cpp.shr"
    if ! "$BUILD_DIR_CPP/$CPP_EXECUTABLE" -d "$input_file" "$cpp_output" >/dev/null 2>&1; then
        printf "%-50s %s\n" "$filename" "FAILED (C++ compression)"
        return 1
    fi
    local cpp_size=$(wc -c < "$cpp_output")
    
    # Compress with C version (if enabled)
    local c_size=0
    if [ "$TEST_C_VERSION" = "true" ]; then
        local c_output="$OUTPUT_DIR/${filename}.c.shr"
        if ! "$BUILD_DIR_C/$C_EXECUTABLE" -d "$input_file" "$c_output" >/dev/null 2>&1; then
            printf "%-50s %s\n" "$filename" "FAILED (C compression)"
            return 1
        fi
        c_size=$(wc -c < "$c_output")
    fi
    
    # Compress with Minishrinkler
    local mini_output="$OUTPUT_DIR/${filename}.mini.shr"
    if ! "$MINI_DIR/$MINI_EXECUTABLE" "$input_file" "$mini_output" >/dev/null 2>&1; then
        printf "%-50s %s\n" "$filename" "FAILED (Mini compression)"
        return 1
    fi
    local mini_size=$(wc -c < "$mini_output")
    
    # Compress with Gzip -9
    local gzip_output="$OUTPUT_DIR/${filename}.gz"
    if ! gzip -9 -c "$input_file" > "$gzip_output" 2>/dev/null; then
        printf "%-50s %s\n" "$filename" "FAILED (Gzip compression)"
        return 1
    fi
    local gzip_size=$(wc -c < "$gzip_output")
    
    # Test decompression for all versions
    local decompression_ok=true
    
    if ! test_decompression "$cpp_output" "$input_file" "Shrinkler"; then
        decompression_ok=false
    fi
    
    if [ "$TEST_C_VERSION" = "true" ] && ! test_decompression "$c_output" "$input_file" "CShrinkler"; then
        decompression_ok=false
    fi
    
    if ! test_decompression "$mini_output" "$input_file" "Minishrinkler"; then
        decompression_ok=false
    fi
    
    if [ "$decompression_ok" = false ]; then
        printf "%-50s %s\n" "$filename" "FAILED (decompression)"
        return 1
    fi
    
    # Calculate ratios
    local cpp_ratio=$(calculate_ratio "$original_size" "$cpp_size")
    local mini_ratio=$(calculate_ratio "$original_size" "$mini_size")
    local gzip_ratio=$(calculate_ratio "$original_size" "$gzip_size")
    
    # Print result
    printf "%-50s %8s %8s %8s %8s\n" "$filename" "${cpp_ratio}%" "${mini_ratio}%" "${gzip_ratio}%" "PASS"
    
    # Update directory statistics
    if [ "$dir_name" = "notes" ]; then
        NOTES_FILES=$((NOTES_FILES + 1))
        NOTES_ORIGINAL_SIZE=$((NOTES_ORIGINAL_SIZE + original_size))
        NOTES_CPP_SIZE=$((NOTES_CPP_SIZE + cpp_size))
        NOTES_MINI_SIZE=$((NOTES_MINI_SIZE + mini_size))
        NOTES_GZIP_SIZE=$((NOTES_GZIP_SIZE + gzip_size))
    elif [ "$dir_name" = "sprites" ]; then
        SPRITES_FILES=$((SPRITES_FILES + 1))
        SPRITES_ORIGINAL_SIZE=$((SPRITES_ORIGINAL_SIZE + original_size))
        SPRITES_CPP_SIZE=$((SPRITES_CPP_SIZE + cpp_size))
        SPRITES_MINI_SIZE=$((SPRITES_MINI_SIZE + mini_size))
        SPRITES_GZIP_SIZE=$((SPRITES_GZIP_SIZE + gzip_size))
    fi
    
    # Update global statistics
    TOTAL_FILES=$((TOTAL_FILES + 1))
    PASSED_FILES=$((PASSED_FILES + 1))
    TOTAL_ORIGINAL_SIZE=$((TOTAL_ORIGINAL_SIZE + original_size))
    TOTAL_CPP_SIZE=$((TOTAL_CPP_SIZE + cpp_size))
    TOTAL_MINI_SIZE=$((TOTAL_MINI_SIZE + mini_size))
    TOTAL_GZIP_SIZE=$((TOTAL_GZIP_SIZE + gzip_size))
    
    if [ "$TEST_C_VERSION" = "true" ]; then
        TOTAL_C_SIZE=$((TOTAL_C_SIZE + c_size))
    fi
    
    return 0
}

# Function to test a directory
test_directory() {
    local dir_path="$1"
    local dir_name=$(basename "$dir_path")
    
    log_info "Testing directory: $dir_name"
    printf "%-50s %8s %8s %8s %8s\n" "Filename" "Shrinkler" "Mini" "Gzip" "Status"
    printf "%-50s %8s %8s %8s %8s\n" "--------------------------------------------------" "--------" "--------" "--------" "--------"
    
    local dir_passed=0
    local dir_total=0
    
    for input_file in "$dir_path"/*; do
        if [ -f "$input_file" ]; then
            dir_total=$((dir_total + 1))
            if test_file "$input_file"; then
                dir_passed=$((dir_passed + 1))
            else
                FAILED_FILES=$((FAILED_FILES + 1))
            fi
        fi
    done
    
    # Print directory summary
    if [ $dir_total -gt 0 ]; then
        local original_size=0
        local cpp_size=0
        local mini_size=0
        local gzip_size=0
        
        if [ "$dir_name" = "notes" ]; then
            original_size=$NOTES_ORIGINAL_SIZE
            cpp_size=$NOTES_CPP_SIZE
            mini_size=$NOTES_MINI_SIZE
            gzip_size=$NOTES_GZIP_SIZE
        elif [ "$dir_name" = "sprites" ]; then
            original_size=$SPRITES_ORIGINAL_SIZE
            cpp_size=$SPRITES_CPP_SIZE
            mini_size=$SPRITES_MINI_SIZE
            gzip_size=$SPRITES_GZIP_SIZE
        fi
        
        if [ $original_size -gt 0 ]; then
            local cpp_ratio=$(echo "scale=1; $cpp_size * 100 / $original_size" | bc -l 2>/dev/null || echo "N/A")
            local mini_ratio=$(echo "scale=1; $mini_size * 100 / $original_size" | bc -l 2>/dev/null || echo "N/A")
            local gzip_ratio=$(echo "scale=1; $gzip_size * 100 / $original_size" | bc -l 2>/dev/null || echo "N/A")
            
            printf "%-50s %8s %8s %8s %8s\n" "--- $dir_name summary ---" "${cpp_ratio}%" "${mini_ratio}%" "${gzip_ratio}%" "$dir_passed/$dir_total"
            echo
        fi
    fi
}

# Main function
main() {
    log_info "=== Shrinkler Compressor Compatibility Test ==="
    if [ "$TEST_C_VERSION" = "true" ]; then
        log_info "Testing: C++ vs C vs Minishrinkler + Decompression verification"
    else
        log_info "Testing: C++ vs Minishrinkler + Decompression verification (C version disabled)"
    fi
    echo
    
    # Compile versions
    compile_versions
    echo
    
    # Check testsuite
    check_testsuite
    echo
    
    # Run tests for each subdirectory
    log_info "Running compatibility tests..."
    echo
    
    for subdir in "$TEST_DIR"/*/; do
        if [ -d "$subdir" ]; then
            test_directory "$subdir"
        fi
    done
    
    echo
    log_info "Tests completed!"
}

# Run main
main "$@"
