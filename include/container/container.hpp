#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "compression/compression.hpp"

namespace bardzip {

inline constexpr std::uint64_t default_max_output = 256ULL * 1024 * 1024;

std::vector<std::uint8_t> build_container(
    std::span<const std::uint8_t> original,
    CompressionMode mode = CompressionMode::None
);

std::vector<std::uint8_t> extract_container(
    std::span<const std::uint8_t> container,
    std::uint64_t max_output = default_max_output
);

}
