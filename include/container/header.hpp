#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "compression/compression.hpp"

namespace bardzip {

struct Header {
    std::uint8_t version = 1;
    CompressionMethod compression = CompressionMethod::None;

    std::uint64_t original_size = 0;
    std::uint64_t payload_size = 0;

    std::uint32_t original_crc32 = 0;
    std::uint32_t payload_crc32 = 0;
};

inline constexpr std::size_t header_size = 36;

using HeaderBytes = std::array<std::uint8_t, header_size>;

HeaderBytes serialize_header(const Header& header);
Header parse_header(std::span<const std::uint8_t> bytes);

}
