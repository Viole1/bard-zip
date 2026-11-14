#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "codec/byte_codec.hpp"
#include "codec/dictionary.hpp"
#include "codec/poem_codec.hpp"
#include "compression/compression.hpp"
#include "container/container.hpp"
#include "container/crc32.hpp"
#include "core/file_operations.hpp"
#include "io/file_writer.hpp"
#include "test_support.hpp"

using namespace bardzip;

namespace {

// Compressible stretches mixed with pseudo-random ones, so every mode has work to do.
std::vector<std::uint8_t> mixed_data(std::size_t size) {
    std::vector<std::uint8_t> data(size);
    std::uint32_t state = 12345;

    for (std::size_t i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        data[i] = (i / 50000) % 2 == 0
            ? static_cast<std::uint8_t>(i % 7)
            : static_cast<std::uint8_t>(state >> 24);
    }

    return data;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    write_binary_file(path, bytes);
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string text;

    for (const auto& line : lines) {
        text += line + "\n";
    }

    return text;
}

std::string read_text(const std::filesystem::path& path) {
    const auto bytes = test::read_all(path);
    return { bytes.begin(), bytes.end() };
}

}

TEST(crc32_incremental_matches_one_shot) {
    const auto data = mixed_data(1000);
    const auto expected = crc32(data);

    for (const std::size_t split : { 0u, 1u, 7u, 500u, 999u, 1000u }) {
        Crc32 crc;
        crc.update(std::span<const std::uint8_t>{ data }.first(split));
        crc.update(std::span<const std::uint8_t>{ data }.subspan(split));
        CHECK(crc.value() == expected);
    }

    CHECK(Crc32{}.value() == 0);
}

TEST(poem_encoder_matches_encode_poem_for_any_chunking) {
    for (const std::size_t size : { 0u, 1u, 2u, 3u, 1001u, 40000u }) {
        const auto data = mixed_data(size);
        const auto expected = encode_poem(data);

        for (const std::size_t chunk : { 1u, 3u, 64u, 100000u }) {
            std::string text;
            std::size_t blocks = 0;

            PoemEncoder encoder([&](std::string_view part) {
                text += part;
                ++blocks;
            });

            for (std::size_t offset = 0; offset < data.size(); offset += chunk) {
                encoder.write(std::span<const std::uint8_t>{ data }
                    .subspan(offset, (std::min)(chunk, data.size() - offset)));
            }

            encoder.finish();

            CHECK(text == expected);
            CHECK(encoder.text_size() == expected.size());
            CHECK(encoder.stanza_count() == size / 2 + size % 2);
        }
    }

    // Large input must be delivered in several blocks, not as one string.
    std::size_t blocks = 0;
    PoemEncoder encoder([&](std::string_view) { ++blocks; });
    encoder.write(mixed_data(100000));
    encoder.finish();
    CHECK(blocks > 3);

    CHECK_THROWS(encoder.write(mixed_data(1)));
}

TEST(zstd_streaming_round_trip_with_tiny_chunks) {
    const auto original = mixed_data(300000);

    std::vector<std::uint8_t> compressed;
    ZstdCompressor compressor(original.size(), [&](std::span<const std::uint8_t> part) {
        compressed.insert(compressed.end(), part.begin(), part.end());
    });

    for (std::size_t offset = 0; offset < original.size(); offset += 4999) {
        compressor.write(std::span<const std::uint8_t>{ original }
            .subspan(offset, (std::min<std::size_t>)(4999, original.size() - offset)));
    }
    compressor.finish();

    std::vector<std::uint8_t> restored;
    std::size_t largest_piece = 0;
    ZstdDecompressor decompressor(original.size(), [&](std::span<const std::uint8_t> part) {
        largest_piece = (std::max)(largest_piece, part.size());
        restored.insert(restored.end(), part.begin(), part.end());
    });

    // One byte at a time is the worst case for the decoder's buffering.
    const std::size_t feed = 1;
    for (std::size_t offset = 0; offset < compressed.size(); offset += feed) {
        decompressor.write(std::span<const std::uint8_t>{ compressed }
            .subspan(offset, (std::min)(feed, compressed.size() - offset)));
    }
    decompressor.finish();

    CHECK(restored == original);
    CHECK(largest_piece <= stream_block_size);
    CHECK(compressed.size() < original.size());
}

TEST(zstd_streaming_decompressor_rejects_bad_streams) {
    const std::vector<std::uint8_t> original(10000, 'a');
    const auto frame = compress(original, CompressionMode::Zstd).bytes;
    const auto ignore = [](std::span<const std::uint8_t>) {};

    {
        ZstdDecompressor decompressor(original.size(), ignore);
        decompressor.write(std::span<const std::uint8_t>{ frame }.first(frame.size() - 3));
        CHECK_THROWS_WITH(decompressor.finish(), "Truncated");
    }

    {
        ZstdDecompressor decompressor(original.size(), ignore);
        decompressor.write(frame);
        decompressor.finish();
        CHECK_THROWS_WITH(decompressor.write(frame), "after zstd frame");
    }

    {
        ZstdDecompressor decompressor(original.size() - 1, ignore);
        CHECK_THROWS_WITH(decompressor.write(frame), "exceeds declared");
    }
}

TEST(poem_stream_rejects_overlong_lines) {
    const std::string poem = std::string(poem_marker) + "\n"
        + std::string(max_poem_line_length + 10, 'x') + "\n";

    std::istringstream stream(poem);
    CHECK_THROWS_WITH(
        decode_poem_stream(stream, {}, [](std::span<const std::uint8_t>) {}),
        "too long"
    );

    std::istringstream no_newline{ std::string(100000, 'x') };
    CHECK_THROWS(decode_poem_stream(no_newline, {}, [](std::span<const std::uint8_t>) {}));
}

TEST(poem_stream_delivers_payload_in_blocks_and_calls_header_first) {
    const auto original = mixed_data(200001);
    const auto container = build_container(original, CompressionMode::None);
    std::istringstream stream(encode_poem(container));

    bool header_seen = false;
    std::size_t blocks = 0;
    std::vector<std::uint8_t> payload;

    const auto header = decode_poem_stream(
        stream,
        [&](const Header& parsed) {
            header_seen = true;
            CHECK(parsed.original_size == original.size());
        },
        [&](std::span<const std::uint8_t> part) {
            CHECK(header_seen);
            ++blocks;
            payload.insert(payload.end(), part.begin(), part.end());
        }
    );

    CHECK(header.payload_size == original.size());
    CHECK(payload == original);
    CHECK(blocks > 1);
}

TEST(file_round_trip_large_multi_block_data_in_every_mode) {
    const auto dir = test::fresh_dir("stream_large");
    const auto original = dir / "large.bin";
    write_bytes(original, mixed_data(400001));

    for (const auto mode :
         { CompressionMode::None, CompressionMode::Zstd, CompressionMode::Auto })
    {
        const auto poem = dir / "large.poem";
        const auto restored = dir / "large.restored";

        const auto report = encode_file(original, poem, mode);
        CHECK(report.original_size == 400001);
        CHECK(report.poem_size == std::filesystem::file_size(poem));
        CHECK(report.stanza_count == (report.container_size + 1) / 2);

        verify_file(poem);
        decode_file(poem, restored);

        CHECK(test::files_equal(original, restored));
        CHECK(test::count_entries(dir) == 3);  // no scratch or temporary files left

        std::filesystem::remove(poem);
        std::filesystem::remove(restored);
    }
}

TEST(streamed_poem_matches_in_memory_poem_for_stored_data) {
    const auto dir = test::fresh_dir("stream_same");
    const auto original = dir / "same.bin";
    const auto poem = dir / "same.poem";
    const auto data = mixed_data(12345);
    write_bytes(original, data);

    encode_file(original, poem, CompressionMode::None);

    CHECK(read_text(poem) == encode_poem(build_container(data, CompressionMode::None)));
}

TEST(streamed_decode_reads_poems_from_in_memory_encoder) {
    const auto dir = test::fresh_dir("stream_compat");
    const auto poem = dir / "compat.poem";
    const auto restored = dir / "compat.restored";
    const auto data = mixed_data(70001);

    write_text_file(poem, encode_poem(build_container(data, CompressionMode::Auto)));
    decode_file(poem, restored);

    CHECK(test::read_all(restored) == data);
}

TEST(damaged_poem_fails_cleanly_without_leftovers) {
    const auto dir = test::fresh_dir("stream_damage");
    const auto original = dir / "damage.bin";
    const auto poem = dir / "damage.poem";
    write_bytes(original, mixed_data(5000));
    encode_file(original, poem, CompressionMode::None);

    auto lines = split_lines(read_text(poem));
    const auto good = lines;

    const auto write_variant = [&](const std::vector<std::string>& variant) {
        const auto path = dir / "variant.poem";
        std::filesystem::remove(path);
        write_text_file(path, join_lines(variant));
        return path;
    };

    const auto output = dir / "damage.out";
    const std::size_t entries_before = test::count_entries(dir) + 1;  // + variant.poem

    // A valid but different word inside the payload: caught by the payload CRC.
    lines[2 + 4 * 100] = encode_byte(0x00, adjectives, nouns);
    if (lines[2 + 4 * 100] == good[2 + 4 * 100]) {
        lines[2 + 4 * 100] = encode_byte(0x01, adjectives, nouns);
    }
    CHECK_THROWS_WITH(decode_file(write_variant(lines), output), "CRC mismatch");
    CHECK_THROWS_WITH(verify_file(write_variant(lines)), "CRC mismatch");

    // Last stanza removed: the poem is shorter than the header promises.
    auto shorter = good;
    shorter.resize(shorter.size() - 4);
    CHECK_THROWS_WITH(decode_file(write_variant(shorter), output), "does not match");

    // Garbage appended.
    auto longer = good;
    longer.insert(longer.end(), good.begin() + 2, good.begin() + 6);
    CHECK_THROWS_WITH(decode_file(write_variant(longer), output), "does not match");

    // Unknown word.
    auto unknown = good;
    unknown[2 + 4 * 50] = "неизвестный баклажан";
    CHECK_THROWS_WITH(decode_file(write_variant(unknown), output), "Failed to decode stanza");

    CHECK(!std::filesystem::exists(output));
    CHECK(test::count_entries(dir) == entries_before);
}

TEST(encode_failure_leaves_no_files_behind) {
    const auto dir = test::fresh_dir("stream_encode_fail");
    const auto original = dir / "in.bin";
    const auto poem = dir / "out.poem";
    write_bytes(original, mixed_data(100000));

    encode_file(original, poem, CompressionMode::Zstd);
    const auto entries = test::count_entries(dir);

    CHECK_THROWS_WITH(encode_file(original, poem, CompressionMode::Zstd), "already exists");
    CHECK_THROWS(encode_file(dir / "missing.bin", dir / "other.poem"));
    CHECK(test::count_entries(dir) == entries);
}

TEST(encode_respects_input_limit) {
    const auto dir = test::fresh_dir("input_limit");
    const auto original = dir / "in.bin";
    const auto poem = dir / "out.poem";
    const auto restored = dir / "restored.bin";
    write_bytes(original, mixed_data(1000));

    CHECK_THROWS_WITH(encode_file(original, poem, CompressionMode::Auto, 999), "Input limit exceeded");
    CHECK_THROWS_WITH(encode_file(original, poem, CompressionMode::Auto, 999), "--max-input");
    CHECK(test::count_entries(dir) == 1);  // nothing was created

    encode_file(original, poem, CompressionMode::Auto, 1000);  // exactly at the limit
    decode_file(poem, restored);
    CHECK(test::files_equal(original, restored));

    std::filesystem::remove(poem);
    encode_file(original, poem, CompressionMode::Auto, 0);  // 0 = no limit
    CHECK(std::filesystem::exists(poem));
}

TEST(inspect_streams_without_decompressing) {
    const auto dir = test::fresh_dir("stream_inspect");
    const auto original = dir / "in.bin";
    const auto poem = dir / "in.poem";
    write_bytes(original, mixed_data(100000));
    encode_file(original, poem, CompressionMode::Zstd);

    inspect_file(poem);

    CHECK(test::count_entries(dir) == 2);
}
