#include "container/crc32.hpp"

#include <array>
#include <cstddef>

namespace bardzip {

namespace {

// Slice-by-8: tables[0] is the classic byte table of the reflected polynomial
// 0xEDB88320; tables[k][i] is the CRC of byte i followed by k zero bytes. Eight
// input bytes are then folded with eight lookups instead of 64 bit steps.
// The result is identical to the bit-by-bit algorithm.
using Tables = std::array<std::array<std::uint32_t, 256>, 8>;

constexpr Tables make_tables() {
    Tables tables{};

    for (std::uint32_t i = 0; i < 256; ++i) {
        auto crc = i;

        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }

        tables[0][i] = crc;
    }

    for (std::size_t k = 1; k < 8; ++k) {
        for (std::size_t i = 0; i < 256; ++i) {
            const auto previous = tables[k - 1][i];
            tables[k][i] = (previous >> 8) ^ tables[0][previous & 0xFF];
        }
    }

    return tables;
}

constexpr Tables tables = make_tables();

// Little-endian load assembled from bytes: independent of the host byte order.
constexpr std::uint32_t load_u32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0])
        | static_cast<std::uint32_t>(p[1]) << 8
        | static_cast<std::uint32_t>(p[2]) << 16
        | static_cast<std::uint32_t>(p[3]) << 24;
}

}

void Crc32::update(std::span<const std::uint8_t> bytes) noexcept {
    auto crc = state_;
    const auto* p = bytes.data();
    auto remaining = bytes.size();

    while (remaining >= 8) {
        const auto low = load_u32(p) ^ crc;
        const auto high = load_u32(p + 4);

        crc = tables[7][low & 0xFF]
            ^ tables[6][(low >> 8) & 0xFF]
            ^ tables[5][(low >> 16) & 0xFF]
            ^ tables[4][low >> 24]
            ^ tables[3][high & 0xFF]
            ^ tables[2][(high >> 8) & 0xFF]
            ^ tables[1][(high >> 16) & 0xFF]
            ^ tables[0][high >> 24];

        p += 8;
        remaining -= 8;
    }

    while (remaining-- != 0) {
        crc = tables[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }

    state_ = crc;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    Crc32 crc;
    crc.update(bytes);
    return crc.value();
}

}
