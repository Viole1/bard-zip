#pragma once

#include <array>
#include <cstdint>

namespace bardzip {

std::array<std::uint8_t, 8> encode_u64_le(std::uint64_t value);
std::uint64_t decode_u64_le(const std::array<std::uint8_t, 8>& bytes);

std::array<std::uint8_t, 4> encode_u32_le(std::uint32_t value);
std::uint32_t decode_u32_le(const std::array<std::uint8_t, 4>& bytes);

}
