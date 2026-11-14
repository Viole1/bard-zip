#include <vector>

#include "compression/compression.hpp"
#include "test_support.hpp"

using namespace bardzip;

namespace {

std::vector<std::uint8_t> repetitive_bytes(std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>('A' + i % 7);
    }
    return bytes;
}

std::vector<std::uint8_t> random_bytes(std::size_t size, std::uint32_t seed = 12345) {
    std::vector<std::uint8_t> bytes(size);
    std::uint32_t state = seed;
    for (auto& byte : bytes) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24);
    }
    return bytes;
}

CompressedPayload zstd_frame(const std::vector<std::uint8_t>& original) {
    return compress(original, CompressionMode::Zstd);
}

}

TEST(zstd_round_trip) {
    for (const std::size_t size : { 0u, 1u, 2u, 3u, 1000u, 100000u, 300000u }) {
        for (const auto& original : { repetitive_bytes(size), random_bytes(size) }) {
            const auto payload = zstd_frame(original);

            CHECK(payload.method == CompressionMethod::Zstd);
            CHECK(decompress(payload.bytes, payload.method, original.size()) == original);
        }
    }
}

TEST(auto_chooses_zstd_for_compressible_data) {
    const auto original = repetitive_bytes(10000);
    const auto payload = compress(original, CompressionMode::Auto);

    CHECK(payload.method == CompressionMethod::Zstd);
    CHECK(payload.bytes.size() < original.size());
}

TEST(auto_chooses_none_for_incompressible_and_tiny_data) {
    const auto noise = random_bytes(2000);
    const auto noisy = compress(noise, CompressionMode::Auto);
    CHECK(noisy.method == CompressionMethod::None);
    CHECK(noisy.bytes == noise);

    const std::vector<std::uint8_t> tiny{ 1, 2, 3 };
    CHECK(compress(tiny, CompressionMode::Auto).method == CompressionMethod::None);

    const auto empty = compress({}, CompressionMode::Auto);
    CHECK(empty.method == CompressionMethod::None);
    CHECK(empty.bytes.empty());
}

TEST(auto_never_grows_payload) {
    for (const std::size_t size : { 0u, 1u, 7u, 64u, 500u, 4096u, 70000u }) {
        for (const auto& original : { repetitive_bytes(size), random_bytes(size, 7) }) {
            const auto payload = compress(original, CompressionMode::Auto);
            CHECK(payload.bytes.size() <= original.size());
        }
    }
}

TEST(none_mode_stores_data_as_is) {
    const auto original = repetitive_bytes(10000);
    const auto payload = compress(original, CompressionMode::None);

    CHECK(payload.method == CompressionMethod::None);
    CHECK(payload.bytes == original);
}

TEST(zstd_mode_compresses_even_when_larger) {
    const std::vector<std::uint8_t> tiny{ 9 };
    const auto payload = compress(tiny, CompressionMode::Zstd);

    CHECK(payload.method == CompressionMethod::Zstd);
    CHECK(payload.bytes.size() > tiny.size());
}

TEST(decompress_rejects_garbage_and_empty_input) {
    const std::vector<std::uint8_t> garbage{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    CHECK_THROWS(decompress(garbage, CompressionMethod::Zstd, 10));
    CHECK_THROWS(decompress({}, CompressionMethod::Zstd, 0));
}

TEST(decompress_rejects_truncated_frame) {
    const auto original = repetitive_bytes(50000);
    auto payload = zstd_frame(original).bytes;

    payload.resize(payload.size() - 3);

    CHECK_THROWS_WITH(
        decompress(payload, CompressionMethod::Zstd, original.size()),
        "Truncated"
    );
}

TEST(decompress_rejects_data_after_frame) {
    const auto original = repetitive_bytes(5000);
    const auto frame = zstd_frame(original).bytes;

    auto with_garbage = frame;
    with_garbage.push_back(0);
    CHECK_THROWS(decompress(with_garbage, CompressionMethod::Zstd, original.size()));

    auto two_frames = frame;
    two_frames.insert(two_frames.end(), frame.begin(), frame.end());
    CHECK_THROWS(decompress(two_frames, CompressionMethod::Zstd, original.size() * 2));
    CHECK_THROWS(decompress(two_frames, CompressionMethod::Zstd, original.size()));
}

TEST(decompress_enforces_declared_size) {
    const auto original = repetitive_bytes(200000);
    const auto frame = zstd_frame(original).bytes;

    CHECK_THROWS_WITH(
        decompress(frame, CompressionMethod::Zstd, original.size() - 1),
        "exceeds declared"
    );
    CHECK_THROWS_WITH(
        decompress(frame, CompressionMethod::Zstd, original.size() + 1),
        "does not match"
    );
}

TEST(decompress_refuses_oversized_zstd_window) {
    // Hand-made empty frames: magic, descriptor (no content size), window
    // descriptor, and one last empty raw block. The window is 2^(10 + exponent).
    const auto empty_frame = [](std::uint8_t exponent) {
        return std::vector<std::uint8_t>{
            0x28, 0xB5, 0x2F, 0xFD, 0x00,
            static_cast<std::uint8_t>(exponent << 3),
            0x01, 0x00, 0x00
        };
    };

    // 2^23 (8 MiB) is fine, 2^27 (128 MiB) must be refused before any allocation.
    CHECK(decompress(empty_frame(13), CompressionMethod::Zstd, 0).empty());
    CHECK_THROWS_WITH(decompress(empty_frame(17), CompressionMethod::Zstd, 0), "Corrupted zstd payload");
}

TEST(decompress_none_checks_size) {
    const std::vector<std::uint8_t> data{ 1, 2, 3 };

    CHECK(decompress(data, CompressionMethod::None, 3) == data);
    CHECK_THROWS(decompress(data, CompressionMethod::None, 4));
}

TEST(compression_method_byte_mapping) {
    CHECK(compression_method_from_byte(0) == CompressionMethod::None);
    CHECK(compression_method_from_byte(1) == CompressionMethod::Zstd);
    CHECK(!compression_method_from_byte(2).has_value());
    CHECK(!compression_method_from_byte(255).has_value());
}
