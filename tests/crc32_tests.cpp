#include <array>
#include <vector>

#include "container/crc32.hpp"
#include "container/endian.hpp"
#include "test_support.hpp"

using namespace bardzip;

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
