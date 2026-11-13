#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*run)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*run)()) {
        registry().push_back({ name, run });
    }
};

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(
            std::string(file) + ":" + std::to_string(line)
            + ": check failed: " + expression
        );
    }
}

template <typename F>
bool throws(F&& action) {
    try {
        action();
    }
    catch (const std::exception&) {
        return true;
    }
    return false;
}

template <typename F>
bool throws_with(F&& action, std::string_view text) {
    try {
        action();
    }
    catch (const std::exception& error) {
        return std::string_view(error.what()).find(text) != std::string_view::npos;
    }
    return false;
}

inline std::filesystem::path work_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "bardzip_tests";
    std::filesystem::create_directories(dir);
    return dir;
}

inline std::filesystem::path fresh_dir(const std::string& name) {
    const auto dir = work_dir() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

inline std::size_t count_entries(const std::filesystem::path& dir) {
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(dir)) {
        ++count;
    }
    return count;
}

inline std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

inline void create_test_file(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);

    for (int value = 0; value < 256; ++value) {
        output.put(static_cast<char>(value));
    }

    output.close();
    if (!output) {
        throw std::runtime_error("Failed to create test file");
    }
}

inline bool files_equal(
    const std::filesystem::path& path1,
    const std::filesystem::path& path2
) {
    std::ifstream file1(path1, std::ios::binary);
    std::ifstream file2(path2, std::ios::binary);

    if (!file1 || !file2) {
        throw std::runtime_error("Failed to open files for comparison");
    }

    const std::vector<char> data1{
        std::istreambuf_iterator<char>(file1), std::istreambuf_iterator<char>() };
    const std::vector<char> data2{
        std::istreambuf_iterator<char>(file2), std::istreambuf_iterator<char>() };

    return data1 == data2;
}

}

#define TEST(name)                                                  \
    static void name();                                             \
    static const test::Registrar name##_registrar{ #name, name };   \
    static void name()

#define CHECK(expr) test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_THROWS(expr) CHECK(test::throws([&] { static_cast<void>(expr); }))
#define CHECK_THROWS_WITH(expr, text) \
    CHECK(test::throws_with([&] { static_cast<void>(expr); }, text))
