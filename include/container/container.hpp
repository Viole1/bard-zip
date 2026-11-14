#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "compression/compression.hpp"

namespace bardzip {

inline constexpr std::uint64_t default_max_output = 256ULL * 1024 * 1024;

// Default limit for the encoder input (16 MiB). A poem is about 54 times larger
// than the data it holds, so this keeps the output near 0.8 GB.
// 0 means "no limit".
inline constexpr std::uint64_t default_max_input = 16ULL * 1024 * 1024;

// Throws if the original size declared in a header exceeds the allowed output.
void ensure_within_output_limit(
    std::uint64_t declared_size,
    std::uint64_t max_output
);

std::vector<std::uint8_t> build_container(
    std::span<const std::uint8_t> original,
    CompressionMode mode = CompressionMode::None
);

std::vector<std::uint8_t> extract_container(
    std::span<const std::uint8_t> container,
    std::uint64_t max_output = default_max_output
);

}
