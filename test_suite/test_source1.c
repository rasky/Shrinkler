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
