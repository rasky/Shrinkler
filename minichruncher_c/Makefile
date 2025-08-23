CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
TARGET = minishrinkler
SOURCE = minishrinkler.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) -lm

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@echo "Testing with small file..."
	@echo "Hello, World! This is a test file for MiniShrinkler compression." > test_input.txt
	./$(TARGET) test_input.txt test_output.shr
	@echo "Test file created: test_input.txt"
	@echo "Compressed file created: test_output.shr"
	@ls -la test_input.txt test_output.shr

.PHONY: all clean test
