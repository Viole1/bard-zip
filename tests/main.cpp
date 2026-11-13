#include <iostream>

#include "test_support.hpp"

int main() {
    int failed = 0;

    for (const auto& test_case : test::registry()) {
        try {
            test_case.run();
            std::cout << "[ OK ] " << test_case.name << '\n';
        }
        catch (const std::exception& error) {
            ++failed;
            std::cout << "[FAIL] " << test_case.name << ": " << error.what() << '\n';
        }
    }

    std::cout << (failed == 0 ? "All tests passed\n" : "Some tests failed\n");
    return failed == 0 ? 0 : 1;
}
