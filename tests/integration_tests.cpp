#include <filesystem>
#include <string>
#include <vector>

#include "core/file_operations.hpp"
#include "io/file_reader.hpp"
#include "io/file_writer.hpp"
#include "test_support.hpp"

using namespace bardzip;

namespace {

void check_file_round_trip(
    const std::filesystem::path& original,
    CompressionMode mode = CompressionMode::Auto
) {
    const auto dir = test::work_dir();
    const auto poem = dir / "roundtrip.poem";
    const auto restored = dir / "roundtrip.restored";

    std::filesystem::remove(poem);
    std::filesystem::remove(restored);

    encode_file(original, poem, mode);
    verify_file(poem);
    decode_file(poem, restored);

    CHECK(test::files_equal(original, restored));
}

}

TEST(file_round_trip_all_byte_values) {
    const auto original = test::work_dir() / "all_bytes.bin";
    std::filesystem::remove(original);
    test::create_test_file(original);

    check_file_round_trip(original);
}

TEST(file_round_trip_empty_and_cyrillic_text) {
    const auto dir = test::work_dir();

    const auto empty = dir / "empty.bin";
    std::filesystem::remove(empty);
    write_binary_file(empty, {});
    check_file_round_trip(empty);

    const auto text = dir / "text.bin";
    std::filesystem::remove(text);
    write_text_file(text, "Привет, мир!\r\nline2\n");
    check_file_round_trip(text);
}

TEST(output_is_never_overwritten) {
    const auto dir = test::work_dir();
    const auto original = dir / "overwrite.bin";
    const auto poem = dir / "overwrite.poem";

    std::filesystem::remove(original);
    std::filesystem::remove(poem);
    test::create_test_file(original);

    encode_file(original, poem);
    CHECK_THROWS(encode_file(original, poem));
}

TEST(file_round_trip_in_every_compression_mode) {
    const auto dir = test::work_dir();

    const auto all_bytes = dir / "modes_all_bytes.bin";
    std::filesystem::remove(all_bytes);
    test::create_test_file(all_bytes);

    const auto repetitive = dir / "modes_repetitive.bin";
    std::filesystem::remove(repetitive);
    write_text_file(repetitive, std::string(50000, 'x'));

    const auto empty = dir / "modes_empty.bin";
    std::filesystem::remove(empty);
    write_binary_file(empty, {});

    for (const auto& file : { all_bytes, repetitive, empty }) {
        for (const auto mode :
             { CompressionMode::None, CompressionMode::Zstd, CompressionMode::Auto })
        {
            check_file_round_trip(file, mode);
        }
    }
}

TEST(encode_report_reflects_compression_choice) {
    const auto dir = test::work_dir();
    const auto repetitive = dir / "report_repetitive.bin";
    const auto poem = dir / "report.poem";

    std::filesystem::remove(repetitive);
    write_text_file(repetitive, std::string(50000, 'x'));

    std::filesystem::remove(poem);
    const auto stored = encode_file(repetitive, poem, CompressionMode::None);
    std::filesystem::remove(poem);
    const auto automatic = encode_file(repetitive, poem, CompressionMode::Auto);

    CHECK(stored.method == CompressionMethod::None);
    CHECK(stored.payload_size == stored.original_size);

    CHECK(automatic.method == CompressionMethod::Zstd);
    CHECK(automatic.payload_size < automatic.original_size);
    CHECK(automatic.poem_size < stored.poem_size);
    CHECK(automatic.poem_size == std::filesystem::file_size(poem));
}

TEST(decode_respects_output_limit) {
    const auto dir = test::work_dir();
    const auto original = dir / "limit.bin";
    const auto poem = dir / "limit.poem";
    const auto restored = dir / "limit.restored";

    std::filesystem::remove(original);
    std::filesystem::remove(poem);
    std::filesystem::remove(restored);
    write_text_file(original, std::string(5000, 'y'));
    encode_file(original, poem, CompressionMode::Zstd);

    CHECK_THROWS_WITH(decode_file(poem, restored, 100), "Output limit exceeded");
    CHECK_THROWS_WITH(decode_file(poem, restored, 100), "--max-output");
    CHECK(!std::filesystem::exists(restored));
    CHECK_THROWS_WITH(verify_file(poem, 100), "Output limit exceeded");
}
