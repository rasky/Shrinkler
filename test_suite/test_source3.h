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
