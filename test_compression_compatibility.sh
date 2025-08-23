#!/bin/bash

# Test script to verify compatibility between C and C++ versions of the Shrinkler compressor
# Author: AI Assistant
# Date: $(date)

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

# Working directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR_C="build/native_c"
BUILD_DIR_CPP="build/native"
TEST_DIR="test_suite"
OUTPUT_DIR="test_output"

# Executable names
C_EXECUTABLE="CShrinkler"
CPP_EXECUTABLE="Shrinkler"

# Counters for final report
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

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
    log_info "Compiling C++ version..."
    if ! make clean && make; then
        log_error "C++ version compilation failed"
        exit 1
    fi
    
    log_info "Compiling C version..."
    if ! make -f Makefile_c clean && make -f Makefile_c; then
        log_error "C version compilation failed"
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
    
    log_success "Compilation completed successfully"
}

# Function to create testsuite
create_testsuite() {
    log_info "Creating testsuite..."
    
    mkdir -p "$TEST_DIR"
    
    # Source files (text)
    local source_files=(
        "test_source1.c"
        "test_source2.cpp"
        "test_source3.h"
        "test_source4.py"
        "test_source5.js"
        "test_source6.txt"
        "test_source7.md"
        "test_source8.sh"
    )
    
    # Binary files (simulated)
    local binary_files=(
        "test_binary1.bin"
        "test_binary2.exe"
        "test_binary3.o"
        "test_binary4.so"
        "test_binary5.dylib"
        "test_binary6.obj"
        "test_binary7.lib"
        "test_binary8.a"
        "test_binary9.dll"
        "test_binary10.bin"
        "test_binary11.bin"
        "test_binary12.bin"
    )
    
    # Create source files with varied content
    for file in "${source_files[@]}"; do
        local size=$((RANDOM % 10000 + 1000))  # 1KB - 11KB
        log_info "Creating source file: $file (${size} bytes)"
        
        # Generate varied content for source files
        case "${file##*.}" in
            "c")
                cat > "$TEST_DIR/$file" << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    printf("Hello, World!\n");
    
    // Some random code
    int array[100];
    for (int i = 0; i < 100; i++) {
        array[i] = i * i;
    }
    
    // More complex logic
    char buffer[256];
    strcpy(buffer, "This is a test string with some content");
    
    return 0;
}
EOF
                ;;
            "cpp")
                cat > "$TEST_DIR/$file" << 'EOF'
#include <iostream>
#include <vector>
#include <string>

class TestClass {
private:
    std::vector<int> data;
    std::string name;
    
public:
    TestClass(const std::string& n) : name(n) {}
    
    void addData(int value) {
        data.push_back(value);
    }
    
    void printData() {
        for (const auto& item : data) {
            std::cout << item << " ";
        }
        std::cout << std::endl;
    }
};

int main() {
    TestClass obj("test");
    for (int i = 0; i < 50; i++) {
        obj.addData(i);
    }
    obj.printData();
    return 0;
}
EOF
                ;;
            "h")
                cat > "$TEST_DIR/$file" << 'EOF'
#ifndef TEST_HEADER_H
#define TEST_HEADER_H

#define MAX_SIZE 1024
#define MIN_SIZE 64

typedef struct {
    int id;
    char name[256];
    double value;
} TestStruct;

extern int global_variable;
extern void test_function(int param);

#endif
EOF
                ;;
            "py")
                cat > "$TEST_DIR/$file" << 'EOF'
#!/usr/bin/env python3

import sys
import os
import json

class TestClass:
    def __init__(self, name):
        self.name = name
        self.data = []
    
    def add_data(self, value):
        self.data.append(value)
    
    def get_sum(self):
        return sum(self.data)

def main():
    obj = TestClass("test_object")
    for i in range(100):
        obj.add_data(i)
    
    print(f"Sum: {obj.get_sum()}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
EOF
                ;;
            "js")
                cat > "$TEST_DIR/$file" << 'EOF'
// JavaScript test file
const fs = require('fs');
const path = require('path');

class TestClass {
    constructor(name) {
        this.name = name;
        this.data = [];
    }
    
    addData(value) {
        this.data.push(value);
    }
    
    getSum() {
        return this.data.reduce((a, b) => a + b, 0);
    }
}

function main() {
    const obj = new TestClass("test");
    for (let i = 0; i < 100; i++) {
        obj.addData(i);
    }
    
    console.log(`Sum: ${obj.getSum()}`);
    return 0;
}

main();
EOF
                ;;
            *)
                # Generic text files
                cat > "$TEST_DIR/$file" << EOF
This is a test file: $file
Generated on: $(date)
Size target: $size bytes

Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.

Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.

EOF
                # Add more content to reach target size
                while [ $(wc -c < "$TEST_DIR/$file") -lt $size ]; do
                    echo "Additional content to reach target size. This is line $(wc -l < "$TEST_DIR/$file")." >> "$TEST_DIR/$file"
                done
                ;;
        esac
    done
    
    # Create binary files with random content
    for file in "${binary_files[@]}"; do
        local size=$((RANDOM % 50000 + 5000))  # 5KB - 55KB
        log_info "Creating binary file: $file (${size} bytes)"
        
        # Generate random binary content
        dd if=/dev/urandom of="$TEST_DIR/$file" bs=1 count=$size 2>/dev/null
    done
    
    log_success "Testsuite created successfully ($(ls "$TEST_DIR" | wc -l) files)"
}

# Function to test a single file
test_file() {
    local input_file="$1"
    local filename=$(basename "$input_file")
    
    log_info "Testing file: $filename"
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Compress with C++ version
    local cpp_output="$OUTPUT_DIR/${filename}.cpp.shr"
    if ! "$BUILD_DIR_CPP/$CPP_EXECUTABLE" -d "$input_file" "$cpp_output" >/dev/null 2>&1; then
        log_error "C++ compression failed for: $filename"
        return 1
    fi
    
    # Compress with C version
    local c_output="$OUTPUT_DIR/${filename}.c.shr"
    if ! "$BUILD_DIR_C/$C_EXECUTABLE" -d "$input_file" "$c_output" >/dev/null 2>&1; then
        log_error "C compression failed for: $filename"
        return 1
    fi
    
    # Compare compressed files
    if cmp -s "$cpp_output" "$c_output"; then
        log_success "✓ $filename: Compressed files are identical"
        return 0
    else
        log_error "✗ $filename: Compressed files are different!"
        log_info "  C++ size: $(wc -c < "$cpp_output") bytes"
        log_info "  C size: $(wc -c < "$c_output") bytes"
        return 1
    fi
}

# Main function
main() {
    log_info "=== Shrinkler Compressor Compatibility Test ==="
    log_info "C version vs C++ version"
    echo
    
    # Compile versions
    compile_versions
    echo
    
    # Create testsuite if it doesn't exist
    if [ ! -d "$TEST_DIR" ]; then
        create_testsuite
        echo
    else
        log_info "Existing testsuite found"
    fi
    
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
        fi
    done
    
    echo
    log_info "Tests completed!"
}

# Run main
main "$@"
