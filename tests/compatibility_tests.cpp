#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cli/arguments.hpp"
#include "core/file_operations.hpp"
#include "io/file_writer.hpp"
#include "test_support.hpp"

using namespace bardzip;

// Golden poems: files created by BardZip 0.1.0 and committed to the repository.
// They freeze the poem format: a future version must still decode them, and
// (for stored data) must still produce exactly these bytes.

namespace {

const std::filesystem::path fixtures_dir = BARDZIP_FIXTURES_DIR;
const std::filesystem::path examples_dir = BARDZIP_EXAMPLES_DIR;

std::vector<std::uint8_t> bytes_of(const std::string& text) {
    return { text.begin(), text.end() };
}

std::vector<std::uint8_t> all_byte_values() {
    std::vector<std::uint8_t> data;
    for (int value = 0; value < 256; ++value) {
        data.push_back(static_cast<std::uint8_t>(value));
    }
    return data;
}

struct Golden {
    const char* file;
    std::vector<std::uint8_t> content;
    bool stored;  // stored data must also be reproduced byte for byte
};

std::vector<Golden> goldens() {
    return {
        { "empty.poem", {}, true },
        { "one_byte.poem", { 0x42 }, true },
        { "seven_bytes.poem", bytes_of("BardZip"), true },
        { "all_byte_values.poem", all_byte_values(), true },
        { "cyrillic_text.poem", bytes_of("Привет, мир!\r\n"), true },
        { "repeated_zstd.poem", std::vector<std::uint8_t>(2000, 'x'), false },
    };
}

}

TEST(golden_poems_decode_to_known_content) {
    const auto dir = test::fresh_dir("golden_decode");

    for (const auto& golden : goldens()) {
        const auto poem = fixtures_dir / golden.file;
        const auto restored = dir / (std::string(golden.file) + ".out");

        verify_file(poem);
        decode_file(poem, restored);

        CHECK(test::read_all(restored) == golden.content);
    }
}

TEST(golden_poems_are_reproduced_exactly_for_stored_data) {
    const auto dir = test::fresh_dir("golden_encode");

    for (const auto& golden : goldens()) {
        if (!golden.stored) {
            continue;  // zstd output may legitimately change between zstd versions
        }

        const auto input = dir / (std::string(golden.file) + ".in");
        const auto poem = dir / golden.file;

        write_binary_file(input, golden.content);
        encode_file(input, poem, CompressionMode::None);

        CHECK(test::read_all(poem) == test::read_all(fixtures_dir / golden.file));
    }
}

TEST(example_poem_decodes_to_hello_text) {
    const auto dir = test::fresh_dir("example_hello");
    const auto restored = dir / "hello.txt";

    verify_file(examples_dir / "hello.poem");
    decode_file(examples_dir / "hello.poem", restored);

    const auto content = test::read_all(restored);
    const std::string text(content.begin(), content.end());

    CHECK(text.starts_with("Hello, BardZip!"));
}

TEST(help_and_version_commands) {
    const auto parse = [](std::vector<std::string> args) {
        args.insert(args.begin(), "bardzip");
        std::vector<char*> argv;
        for (auto& arg : args) {
            argv.push_back(arg.data());
        }
        return parse_arguments(static_cast<int>(argv.size()), argv.data());
    };

    CHECK(parse({ "--help" }).command == Command::Help);
    CHECK(parse({ "-h" }).command == Command::Help);
    CHECK(parse({ "help" }).command == Command::Help);
    CHECK(parse({ "encode", "--help" }).command == Command::Help);
    CHECK(parse({ "decode", "in", "-o", "out", "-h" }).command == Command::Help);
    CHECK(parse({ "--version" }).command == Command::Version);
    CHECK(parse({ "-V" }).command == Command::Version);

    CHECK(version_text().starts_with("bardzip "));
    CHECK(help_text().starts_with(version_text().substr(0, version_text().size() - 1)));

    // The limits shown in --help must match the real defaults.
    const auto mib = [](std::uint64_t bytes) { return std::to_string(bytes >> 20) + " MiB"; };
    CHECK(help_text().find("Default: " + mib(default_max_input)) != std::string::npos);
    CHECK(help_text().find("Default: " + mib(default_max_output)) != std::string::npos);

    CHECK_THROWS(parse({}));
}
