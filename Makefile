# Unified Makefile for Shrinkler project
# Targets: compressore C, compressore C++, minishrinkler, decompressore C

ifndef PLATFORM
PLATFORM := native
endif

ifneq ($(PLATFORM),$(filter $(PLATFORM),amiga windows-32 windows-64 native native-32 native-64 mac))
DUMMY := $(error Unsupported platform $(PLATFORM))
endif

# Build directories
BUILD_DIR_CPP    := build/$(PLATFORM)
BUILD_DIR_C      := build/$(PLATFORM)_c
BUILD_DIR_MINI   := minichruncher_c
BUILD_DIR_DEC    := decruncher_c

# Include paths
INCLUDE := -I decrunchers_bin

# Create build directories
MKDIR_DUMMY := $(shell mkdir -p $(BUILD_DIR_CPP) $(BUILD_DIR_C))

# Default target
all: cpp-compressor c-compressor minishrinkler decompressor

# Individual targets
cpp-compressor: $(BUILD_DIR_CPP)/Shrinkler
c-compressor: $(BUILD_DIR_C)/CShrinkler
minishrinkler: $(BUILD_DIR_MINI)/minishrinkler
decompressor: $(BUILD_DIR_DEC)/shrinkler_dec

# Common flags
CFLAGS := -Wall -Wno-sign-compare
LFLAGS :=

ifdef DEBUG
CFLAGS += -g -DDEBUG
else
CFLAGS += -O3
ifneq ($(PLATFORM),mac)
LFLAGS += -s
endif
endif

ifdef PROFILE
CFLAGS += -fno-inline -fno-inline-functions
LFLAGS :=
endif

# Platform-specific settings
ifeq ($(PLATFORM),amiga)
# Amiga build
CC_CPP     := m68k-amigaos-g++
CC_C       := m68k-amigaos-gcc
LINK_CPP   := m68k-amigaos-g++
LINK_C     := m68k-amigaos-gcc
CFLAGS     += -m68000
LFLAGS     += -noixemul

else ifeq ($(PLATFORM),windows-32)
# 32-bit MinGW build
CC_CPP     := i686-w64-mingw32-g++
CC_C       := i686-w64-mingw32-gcc
LINK_CPP   := i686-w64-mingw32-g++
LINK_C     := i686-w64-mingw32-gcc
LFLAGS     += -static -static-libgcc -static-libstdc++

else ifeq ($(PLATFORM),windows-64)
# 64-bit MinGW build
CC_CPP     := x86_64-w64-mingw32-g++
CC_C       := x86_64-w64-mingw32-gcc
LINK_CPP   := x86_64-w64-mingw32-g++
LINK_C     := x86_64-w64-mingw32-gcc
LFLAGS     += -static -static-libgcc -static-libstdc++

else
# Native build
CC_CPP     := g++
CC_C       := gcc
LINK_CPP   := g++
LINK_C     := gcc

ifeq ($(PLATFORM),native-32)
CFLAGS     += -m32
LFLAGS     += -m32
endif

ifeq ($(PLATFORM),native-64)
CFLAGS     += -m64
LFLAGS     += -m64
endif

ifeq ($(PLATFORM),mac)
CFLAGS     += -mmacosx-version-min=10.9
LFLAGS     += -mmacosx-version-min=10.9
endif

endif

# C++ Compressor (Shrinkler)
$(BUILD_DIR_CPP)/%.o: cruncher/%.cpp
	$(CC_CPP) $(CFLAGS) $(INCLUDE) $< -c -o $@

$(BUILD_DIR_CPP)/Shrinkler: $(BUILD_DIR_CPP)/Shrinkler.o
	$(LINK_CPP) $(LFLAGS) $< -o $@

# C Compressor (CShrinkler)
$(BUILD_DIR_C)/%.o: cruncher_c/%.c
	$(CC_C) $(CFLAGS) $(INCLUDE) $< -c -o $@

$(BUILD_DIR_C)/CShrinkler: $(BUILD_DIR_C)/Shrinkler.o $(BUILD_DIR_C)/DataFile.o $(BUILD_DIR_C)/HunkFile.o $(BUILD_DIR_C)/Pack.o $(BUILD_DIR_C)/RangeCoder.o $(BUILD_DIR_C)/Coder.o $(BUILD_DIR_C)/LZEncoder.o $(BUILD_DIR_C)/MatchFinder.o $(BUILD_DIR_C)/LZParser.o $(BUILD_DIR_C)/SuffixArray.o $(BUILD_DIR_C)/CountingCoder.o $(BUILD_DIR_C)/SizeMeasuringCoder.o $(BUILD_DIR_C)/LZProgress.o $(BUILD_DIR_C)/RefEdge.o $(BUILD_DIR_C)/Heap.o $(BUILD_DIR_C)/CuckooHash.o
	$(LINK_C) $(LFLAGS) $^ -o $@

# Minishrinkler
$(BUILD_DIR_MINI)/minishrinkler: $(BUILD_DIR_MINI)/minishrinkler_api.c $(BUILD_DIR_MINI)/minishrinkler.c $(BUILD_DIR_MINI)/minishrinkler.h
	$(CC_C) -Wall -Wextra -O2 -std=c99 -o $@ $(BUILD_DIR_MINI)/minishrinkler_api.c $(BUILD_DIR_MINI)/minishrinkler.c -lm

# Decompressor
$(BUILD_DIR_DEC)/shrinkler_dec: $(BUILD_DIR_DEC)/shrinkler_dec.c
	$(CC_C) -Wall -Wextra -O2 -std=c99 -o $@ $<

# Header dependencies
HEADERS := Header1.dat Header1C.dat Header1T.dat Header1CT.dat Header2.dat Header2C.dat
HEADERS += OverlapHeader.dat OverlapHeaderC.dat OverlapHeaderT.dat OverlapHeaderCT.dat
HEADERS += MiniHeader.dat MiniHeaderC.dat

$(BUILD_DIR_CPP)/Shrinkler.o: cruncher/*.h $(patsubst %,decrunchers_bin/%,$(HEADERS))
$(BUILD_DIR_C)/CShrinkler.o: cruncher_c/*.h $(patsubst %,decrunchers_bin/%,$(HEADERS))

# Generate header files from binary files
%.dat: %.bin
	python3 -c 'print(", ".join("0x%02X" % b for b in open("$^", "rb").read()))' > $@

# Clean targets
clean:
	rm -rf build decrunchers_bin/*.dat
	rm -f $(BUILD_DIR_MINI)/minishrinkler
	rm -f $(BUILD_DIR_DEC)/shrinkler_dec

clean-cpp:
	rm -rf $(BUILD_DIR_CPP)

clean-c:
	rm -rf $(BUILD_DIR_C)

clean-mini:
	rm -f $(BUILD_DIR_MINI)/minishrinkler

clean-decompressor:
	rm -f $(BUILD_DIR_DEC)/shrinkler_dec

# Test targets
test: all
	@echo "Running compatibility tests..."
	./test_compression_compatibility.sh

test-mini: minishrinkler
	@echo "Testing minishrinkler..."
	@echo "Hello, World! This is a test file for MiniShrinkler compression." > $(BUILD_DIR_MINI)/test_input.txt
	$(BUILD_DIR_MINI)/minishrinkler $(BUILD_DIR_MINI)/test_input.txt $(BUILD_DIR_MINI)/test_output.shr
	@echo "Test file created: $(BUILD_DIR_MINI)/test_input.txt"
	@echo "Compressed file created: $(BUILD_DIR_MINI)/test_output.shr"
	@ls -la $(BUILD_DIR_MINI)/test_input.txt $(BUILD_DIR_MINI)/test_output.shr

test-decompressor: decompressor
	@echo "Testing decompressor..."
	@echo "Note: This requires a compressed file to test with"
	@echo "Usage: ./$(BUILD_DIR_DEC)/shrinkler_dec -v compressed_file.shr output_file"

# Install targets
install: all
	cp $(BUILD_DIR_CPP)/Shrinkler /usr/local/bin/
	cp $(BUILD_DIR_C)/CShrinkler /usr/local/bin/
	cp $(BUILD_DIR_MINI)/minishrinkler /usr/local/bin/
	cp $(BUILD_DIR_DEC)/shrinkler_dec /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/Shrinkler
	rm -f /usr/local/bin/CShrinkler
	rm -f /usr/local/bin/minishrinkler
	rm -f /usr/local/bin/shrinkler_dec

# Help target
help:
	@echo "Available targets:"
	@echo "  all              - Build all targets (default)"
	@echo "  cpp-compressor   - Build C++ compressor (Shrinkler)"
	@echo "  c-compressor     - Build C compressor (CShrinkler)"
	@echo "  minishrinkler    - Build minishrinkler"
	@echo "  decompressor     - Build decompressor"
	@echo ""
	@echo "  test             - Run compatibility tests"
	@echo "  test-mini        - Test minishrinkler"
	@echo "  test-decompressor - Test decompressor"
	@echo ""
	@echo "  clean            - Clean all build artifacts"
	@echo "  clean-cpp        - Clean C++ build"
	@echo "  clean-c          - Clean C build"
	@echo "  clean-mini       - Clean minishrinkler"
	@echo "  clean-decompressor - Clean decompressor"
	@echo ""
	@echo "  install          - Install all executables to /usr/local/bin"
	@echo "  uninstall        - Remove installed executables"
	@echo ""
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  PLATFORM         - Target platform (native, amiga, windows-32, windows-64, mac)"
	@echo "  DEBUG            - Enable debug build"
	@echo "  PROFILE          - Enable profiling build"

.PHONY: all cpp-compressor c-compressor minishrinkler decompressor clean clean-cpp clean-c clean-mini clean-decompressor test test-mini test-decompressor install uninstall help
