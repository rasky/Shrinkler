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
