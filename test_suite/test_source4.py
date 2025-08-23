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
