#!/bin/bash

# Test script to verify compatibility between C, C++ and Mini versions of the Shrinkler compressor
# Author: AI Assistant
# Date: $(date)

set -e  # Exit on any error

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

# Counters for final report
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Compression ratio statistics
TOTAL_ORIGINAL_SIZE=0
TOTAL_C_COMPRESSED_SIZE=0
TOTAL_CPP_COMPRESSED_SIZE=0
TOTAL_MINI_COMPRESSED_SIZE=0

# Function to clean temporary files
cleanup() {
    log_info "Cleaning temporary files..."
    rm -rf "$OUTPUT_DIR"
}

# Function to handle exit
exit_handler() {
    cleanup
    echo
    log_info "=== FINAL REPORT ==="
    log_info "Total tests: $TOTAL_TESTS"
    log_success "Passed tests: $PASSED_TESTS"
    if [ $FAILED_TESTS -gt 0 ]; then
        log_error "Failed tests: $FAILED_TESTS"
    fi
    
    if [ $TOTAL_ORIGINAL_SIZE -gt 0 ]; then
        echo
        log_info "=== COMPRESSION RATIO SUMMARY ==="
        local total_c_ratio=$(echo "scale=2; $TOTAL_C_COMPRESSED_SIZE * 100 / $TOTAL_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
        local total_cpp_ratio=$(echo "scale=2; $TOTAL_CPP_COMPRESSED_SIZE * 100 / $TOTAL_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
        local total_mini_ratio=$(echo "scale=2; $TOTAL_MINI_COMPRESSED_SIZE * 100 / $TOTAL_ORIGINAL_SIZE" | bc -l 2>/dev/null || echo "N/A")
        
        log_info "Total compression ratios (${TOTAL_ORIGINAL_SIZE} bytes total):"
        log_info "  CShrinkler: ${TOTAL_C_COMPRESSED_SIZE} bytes (${total_c_ratio}%)"
        log_info "  Shrinkler:  ${TOTAL_CPP_COMPRESSED_SIZE} bytes (${total_cpp_ratio}%)"
        log_mini "  Minishrinkler: ${TOTAL_MINI_COMPRESSED_SIZE} bytes (${total_mini_ratio}%)"
        
        if [ "$total_c_ratio" != "N/A" ] && [ "$total_cpp_ratio" != "N/A" ] && [ "$total_mini_ratio" != "N/A" ]; then
            echo
            log_info "Performance comparison:"
            if (( $(echo "$total_mini_ratio < $total_cpp_ratio" | bc -l) )); then
                log_success "✓ Minishrinkler beats Shrinkler by $(echo "scale=2; $total_cpp_ratio - $total_mini_ratio" | bc -l)%"
            else
                log_warning "⚠ Minishrinkler is $(echo "scale=2; $total_mini_ratio - $total_cpp_ratio" | bc -l)% worse than Shrinkler"
            fi
            
            if (( $(echo "$total_mini_ratio < $total_c_ratio" | bc -l) )); then
                log_success "✓ Minishrinkler beats CShrinkler by $(echo "scale=2; $total_c_ratio - $total_mini_ratio" | bc -l)%"
            else
                log_warning "⚠ Minishrinkler is $(echo "scale=2; $total_mini_ratio - $total_c_ratio" | bc -l)% worse than CShrinkler"
            fi
        fi
    fi
    
    if [ $FAILED_TESTS -gt 0 ]; then
        exit 1
    else
        log_success "All tests passed!"
        exit 0
    fi
}

# Set trap for cleanup
trap exit_handler EXIT INT TERM

# Function to compile versions
compile_versions() {
    log_info "Compiling all versions..."
    if ! make all; then
        log_error "Compilation failed"
        exit 1
    fi
    
    # Compile minishrinkler
    log_info "Compiling minishrinkler..."
    if ! (cd "$MINI_DIR" && make clean && make); then
        log_error "Minishrinkler compilation failed"
        exit 1
    fi
    
    # Verify that executables exist
    if [ ! -f "$BUILD_DIR_CPP/$CPP_EXECUTABLE" ]; then
        log_error "C++ executable not found: $BUILD_DIR_CPP/$CPP_EXECUTABLE"
        exit 1
    fi
    
    if [ ! -f "$BUILD_DIR_C/$C_EXECUTABLE" ]; then
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
    
    local file_count=$(ls "$TEST_DIR" | wc -l)
    if [ $file_count -eq 0 ]; then
        log_error "No test files found in $TEST_DIR"
        exit 1
    fi
    
    log_info "Found $file_count test files in $TEST_DIR"
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
    local filename=$(basename "$original_file")
    
    # Decompress using our decompressor
    local decompressed_output="$OUTPUT_DIR/${filename}.${compressor_name}.decompressed"
    if ! "./decruncher_c/shrinkler_dec" "$compressed_file" > "$decompressed_output" 2>/dev/null; then
        log_error "Decompression failed for $compressor_name: $filename"
        return 1
    fi
    
    # Compare with original
    if cmp -s "$original_file" "$decompressed_output"; then
        log_success "✓ $filename: $compressor_name decompression successful (matches original)"
        return 0
    else
        log_error "✗ $filename: $compressor_name decompression failed (doesn't match original)"
        log_info "  Original size: $(wc -c < "$original_file") bytes"
        log_info "  Decompressed size: $(wc -c < "$decompressed_output") bytes"
        return 1
    fi
}

# Function to test a single file
test_file() {
    local input_file="$1"
    local filename=$(basename "$input_file")
    local original_size=$(wc -c < "$input_file")
    
    log_info "Testing file: $filename (${original_size} bytes)"
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Compress with C++ version
    local cpp_output="$OUTPUT_DIR/${filename}.cpp.shr"
    if ! "$BUILD_DIR_CPP/$CPP_EXECUTABLE" -d "$input_file" "$cpp_output" >/dev/null 2>&1; then
        log_error "C++ compression failed for: $filename"
        return 1
    fi
    local cpp_size=$(wc -c < "$cpp_output")
    local cpp_ratio=$(calculate_ratio "$original_size" "$cpp_size")
    
    # Compress with C version
    local c_output="$OUTPUT_DIR/${filename}.c.shr"
    if ! "$BUILD_DIR_C/$C_EXECUTABLE" -d "$input_file" "$c_output" >/dev/null 2>&1; then
        log_error "C compression failed for: $filename"
        return 1
    fi
    local c_size=$(wc -c < "$c_output")
    local c_ratio=$(calculate_ratio "$original_size" "$c_size")
    
    # Compress with Minishrinkler
    local mini_output="$OUTPUT_DIR/${filename}.mini.shr"
    if ! "$MINI_DIR/$MINI_EXECUTABLE" "$input_file" "$mini_output" >/dev/null 2>&1; then
        log_error "Minishrinkler compression failed for: $filename"
        return 1
    fi
    local mini_size=$(wc -c < "$mini_output")
    local mini_ratio=$(calculate_ratio "$original_size" "$mini_size")
    
    # Test decompression for all three versions
    local decompression_ok=true
    
    if ! test_decompression "$cpp_output" "$input_file" "Shrinkler"; then
        decompression_ok=false
    fi
    
    if ! test_decompression "$c_output" "$input_file" "CShrinkler"; then
        decompression_ok=false
    fi
    
    if ! test_decompression "$mini_output" "$input_file" "Minishrinkler"; then
        decompression_ok=false
    fi
    
    if [ "$decompression_ok" = false ]; then
        return 1
    fi
    
    # Compare compressed files (C vs C++)
    if cmp -s "$cpp_output" "$c_output"; then
        log_success "✓ $filename: C and C++ compressed files are identical"
    else
        log_warning "⚠ $filename: C and C++ compressed files are different!"
        log_info "  C++ size: ${cpp_size} bytes (${cpp_ratio}%)"
        log_info "  C size: ${c_size} bytes (${c_ratio}%)"
    fi
    
    # Compare with Minishrinkler
    if cmp -s "$cpp_output" "$mini_output"; then
        log_success "✓ $filename: Minishrinkler and C++ compressed files are identical"
    elif cmp -s "$c_output" "$mini_output"; then
        log_success "✓ $filename: Minishrinkler and C compressed files are identical"
    else
        log_mini "ℹ $filename: Minishrinkler generates different but valid bitstream"
        log_info "  Minishrinkler size: ${mini_size} bytes (${mini_ratio}%)"
    fi
    
    # Print compression ratios
    log_info "Compression ratios for $filename:"
    log_info "  Shrinkler:     ${cpp_size} bytes (${cpp_ratio}%)"
    log_info "  CShrinkler:    ${c_size} bytes (${c_ratio}%)"
    log_mini "  Minishrinkler: ${mini_size} bytes (${mini_ratio}%)"
    
    # Update statistics
    TOTAL_ORIGINAL_SIZE=$((TOTAL_ORIGINAL_SIZE + original_size))
    TOTAL_C_COMPRESSED_SIZE=$((TOTAL_C_COMPRESSED_SIZE + c_size))
    TOTAL_CPP_COMPRESSED_SIZE=$((TOTAL_CPP_COMPRESSED_SIZE + cpp_size))
    TOTAL_MINI_COMPRESSED_SIZE=$((TOTAL_MINI_COMPRESSED_SIZE + mini_size))
    
    return 0
}

# Main function
main() {
    log_info "=== Shrinkler Compressor Compatibility Test ==="
    log_info "C version vs C++ version vs Minishrinkler + Decompression verification"
    echo
    
    # Compile versions
    compile_versions
    echo
    
    # Check testsuite
    check_testsuite
    echo
    
    # Run tests
    log_info "Running compatibility tests..."
    echo
    
    for input_file in "$TEST_DIR"/*; do
        if [ -f "$input_file" ]; then
            TOTAL_TESTS=$((TOTAL_TESTS + 1))
            
            if test_file "$input_file"; then
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            echo
        fi
    done
    
    echo
    log_info "Tests completed!"
}

# Run main
main "$@"
