#include <vector>

#include "container/container.hpp"
#include "container/crc32.hpp"
#include "container/endian.hpp"
#include "container/header.hpp"
#include "test_support.hpp"

using namespace bardzip;

namespace {

std::vector<std::uint8_t> assemble(
    const Header& header,
    std::span<const std::uint8_t> payload
) {
    const auto header_bytes = serialize_header(header);

    std::vector<std::uint8_t> container(header_bytes.begin(), header_bytes.end());
    container.insert(container.end(), payload.begin(), payload.end());
    return container;
}

Header zstd_header(
    std::span<const std::uint8_t> original,
    std::span<const std::uint8_t> frame
) {
    Header header;
    header.compression = CompressionMethod::Zstd;
    header.original_size = original.size();
    header.payload_size = frame.size();
    header.original_crc32 = crc32(original);
    header.payload_crc32 = crc32(frame);
    return header;
}

std::vector<std::uint8_t> zeros(std::size_t size) {
    return std::vector<std::uint8_t>(size, 0);
}

}

TEST(header_round_trip) {
    Header header;
    header.original_size = 0x0102030405060708ULL;
    header.payload_size = header.original_size;
    header.original_crc32 = 0x12345678u;
    header.payload_crc32 = header.original_crc32;

    const auto bytes = serialize_header(header);
    const auto restored = parse_header(bytes);

    CHECK(bytes.size() == header_size);
    CHECK(bytes[0] == 'B' && bytes[1] == 'Z' && bytes[2] == 'P' && bytes[3] == '1');
    CHECK(restored.original_size == header.original_size);
    CHECK(restored.original_crc32 == header.original_crc32);
}

TEST(header_rejects_damage) {
    auto bytes = serialize_header(Header{});

    auto bad_magic = bytes;
    bad_magic[0] ^= 0x01;
    CHECK_THROWS(parse_header(bad_magic));

    auto bad_crc = bytes;
    bad_crc[8] ^= 0x01;
    CHECK_THROWS(parse_header(bad_crc));
}

TEST(container_round_trip_small_sizes) {
    for (std::size_t size = 0; size < 6; ++size) {
        std::vector<std::uint8_t> original(size);
        for (std::size_t i = 0; i < size; ++i) {
            original[i] = static_cast<std::uint8_t>(0xF0 + i);
        }

        const auto container = build_container(original);
        CHECK(container.size() == header_size + size);
        CHECK(extract_container(container) == original);
    }
}

TEST(container_rejects_payload_damage) {
    const std::vector<std::uint8_t> original{ 0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF };

    auto container = build_container(original);
    container[header_size + 1] ^= 0x01;

    CHECK_THROWS(extract_container(container));
}

TEST(container_rejects_truncation_and_extra_bytes) {
    const std::vector<std::uint8_t> original{ 1, 2, 3 };
    const auto container = build_container(original);

    auto truncated = container;
    truncated.pop_back();
    CHECK_THROWS(extract_container(truncated));

    auto extended = container;
    extended.push_back(0);
    CHECK_THROWS(extract_container(extended));
}

TEST(container_zstd_round_trip) {
    for (const std::size_t size : { 0u, 1u, 3u, 1000u, 100000u }) {
        std::vector<std::uint8_t> original(size);
        for (std::size_t i = 0; i < size; ++i) {
            original[i] = static_cast<std::uint8_t>(i % 5);
        }

        const auto container = build_container(original, CompressionMode::Zstd);

        CHECK(parse_header(std::span{ container }.first(header_size)).compression
              == CompressionMethod::Zstd);
        CHECK(extract_container(container) == original);
    }
}

TEST(container_auto_records_chosen_method) {
    const auto compressible = zeros(10000);
    const auto compressed = build_container(compressible, CompressionMode::Auto);
    const auto compressed_header = parse_header(std::span{ compressed }.first(header_size));

    CHECK(compressed_header.compression == CompressionMethod::Zstd);
    CHECK(compressed_header.payload_size < compressed_header.original_size);
    CHECK(extract_container(compressed) == compressible);

    const std::vector<std::uint8_t> tiny{ 1, 2, 3 };
    const auto stored = build_container(tiny, CompressionMode::Auto);

    CHECK(parse_header(std::span{ stored }.first(header_size)).compression
          == CompressionMethod::None);
    CHECK(extract_container(stored) == tiny);
}

TEST(container_zstd_rejects_corrupted_payload) {
    const auto original = zeros(10000);
    auto container = build_container(original, CompressionMode::Zstd);

    container[header_size + 3] ^= 0x40;

    CHECK_THROWS_WITH(extract_container(container), "Payload CRC mismatch");
}

TEST(container_zstd_rejects_wrong_original_crc) {
    const auto original = zeros(1000);
    const auto frame = compress(original, CompressionMode::Zstd).bytes;

    auto header = zstd_header(original, frame);
    header.original_crc32 ^= 1;

    CHECK_THROWS_WITH(extract_container(assemble(header, frame)), "Original CRC mismatch");
}

TEST(container_zstd_rejects_wrong_original_size) {
    const auto original = zeros(100000);
    const auto frame = compress(original, CompressionMode::Zstd).bytes;

    auto smaller = zstd_header(original, frame);
    smaller.original_size = 10;
    CHECK_THROWS_WITH(extract_container(assemble(smaller, frame)), "exceeds declared");

    auto larger = zstd_header(original, frame);
    larger.original_size = original.size() + 1;
    CHECK_THROWS_WITH(extract_container(assemble(larger, frame)), "does not match");
}

TEST(container_zstd_rejects_data_after_frame) {
    const auto original = zeros(1000);
    auto payload = compress(original, CompressionMode::Zstd).bytes;
    payload.push_back(0xAB);

    const auto header = zstd_header(original, payload);

    CHECK_THROWS_WITH(extract_container(assemble(header, payload)), "after zstd frame");
}

TEST(container_enforces_output_limit_before_decompression) {
    const auto original = zeros(100000);
    const auto container = build_container(original, CompressionMode::Zstd);

    CHECK_THROWS_WITH(extract_container(container, 1000), "Output limit exceeded");
    CHECK_THROWS_WITH(extract_container(container, original.size() - 1), "Output limit exceeded");
    CHECK(extract_container(container, original.size()) == original);

    const auto stored = build_container(zeros(100), CompressionMode::None);
    CHECK_THROWS_WITH(extract_container(stored, 99), "Output limit exceeded");
}

TEST(header_rejects_unknown_compression_even_with_valid_crc) {
    auto bytes = serialize_header(Header{});
    bytes[5] = 2;

    const auto checksum = encode_u32_le(crc32(std::span{ bytes }.first(32)));
    std::copy(checksum.begin(), checksum.end(), bytes.begin() + 32);

    CHECK_THROWS_WITH(parse_header(bytes), "Unsupported compression method");
}

TEST(header_validates_fields_per_compression_method) {
    Header stored;
    stored.original_size = 5;
    stored.payload_size = 4;
    CHECK_THROWS(serialize_header(stored));

    Header empty_zstd;
    empty_zstd.compression = CompressionMethod::Zstd;
    CHECK_THROWS(serialize_header(empty_zstd));

    Header valid_zstd;
    valid_zstd.compression = CompressionMethod::Zstd;
    valid_zstd.original_size = 100;
    valid_zstd.payload_size = 12;
    valid_zstd.original_crc32 = 1;
    valid_zstd.payload_crc32 = 2;
    CHECK(parse_header(serialize_header(valid_zstd)).compression == CompressionMethod::Zstd);
}
