#include "container/endian.hpp"

#include <cstddef>

namespace bardzip {

std::array<std::uint8_t, 8> encode_u64_le(std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes;

    for (std::size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }

    return bytes;
}

std::uint64_t decode_u64_le(const std::array<std::uint8_t, 8>& bytes) {
    std::uint64_t value = 0;

    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }

    return value;
}

std::array<std::uint8_t, 4> encode_u32_le(std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes;

    for (std::size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }

    return bytes;
}

std::uint32_t decode_u32_le(const std::array<std::uint8_t, 4>& bytes) {
    std::uint32_t value = 0;

    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[i]) << (i * 8);
    }

    return value;
}

}
