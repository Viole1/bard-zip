#include <algorithm>
#include <array>
#include <vector>

#include "container/crc32.hpp"
#include "container/endian.hpp"
#include "test_support.hpp"

using namespace bardzip;

namespace {

// The original bit-by-bit algorithm, kept as the reference for the table version.
std::uint32_t reference_crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFFu;

    for (const auto byte : bytes) {
        crc ^= byte;

        for (int i = 0; i < 8; ++i) {
            crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }

    return ~crc;
}

}

TEST(crc32_matches_bitwise_reference_for_all_lengths_and_alignments) {
    std::vector<std::uint8_t> data(600);
    std::uint32_t state = 7;

    for (auto& byte : data) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24);
    }

    // Lengths around the 8-byte block size, every start offset, all-ones and zero data.
    for (std::size_t offset = 0; offset < 9; ++offset) {
        for (std::size_t length = 0; length <= 300; ++length) {
            const std::span<const std::uint8_t> view{ data.data() + offset, length };
            CHECK(crc32(view) == reference_crc32(view));
        }
    }

    const std::vector<std::uint8_t> ones(1000, 0xFF);
    const std::vector<std::uint8_t> zeros(1000, 0x00);
    CHECK(crc32(ones) == reference_crc32(ones));
    CHECK(crc32(zeros) == reference_crc32(zeros));

    // Incremental updates with awkward chunk sizes give the same result.
    for (const std::size_t chunk : { 1u, 3u, 7u, 8u, 9u, 64u }) {
        Crc32 crc;
        for (std::size_t i = 0; i < data.size(); i += chunk) {
            crc.update(std::span<const std::uint8_t>{ data }.subspan(i, (std::min)(chunk, data.size() - i)));
        }
        CHECK(crc.value() == reference_crc32(data));
    }
}

TEST(crc32_empty_vector) {
    const std::array<std::uint8_t, 0> empty{};
    CHECK(crc32(empty) == 0x00000000u);
}

TEST(crc32_reference_vector) {
    constexpr std::array<std::uint8_t, 9> input{
        0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39
    };
    CHECK(crc32(input) == 0xCBF43926u);
}

TEST(u64_le_layout_and_round_trip) {
    const auto bytes = encode_u64_le(0x0102030405060708ULL);
    const std::array<std::uint8_t, 8> expected{
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
    };
    CHECK(bytes == expected);

    for (const std::uint64_t value :
         { 0ULL, 1ULL, 255ULL, 256ULL, 0x0102030405060708ULL, 0xFFFFFFFFFFFFFFFFULL })
    {
        CHECK(decode_u64_le(encode_u64_le(value)) == value);
    }
}

TEST(u32_le_layout_and_round_trip) {
    const std::array<std::uint8_t, 4> expected{ 0x78, 0x56, 0x34, 0x12 };
    CHECK(encode_u32_le(0x12345678u) == expected);
    CHECK(decode_u32_le(expected) == 0x12345678u);
}
