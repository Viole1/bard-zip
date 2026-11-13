#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bardzip {

enum class CompressionMethod : std::uint8_t {
    None = 0,
    Zstd = 1
};

enum class CompressionMode {
    None,
    Zstd,
    Auto
};

struct CompressedPayload {
    CompressionMethod method = CompressionMethod::None;
    std::vector<std::uint8_t> bytes;
};

std::optional<CompressionMethod> compression_method_from_byte(std::uint8_t value);
std::string_view to_string(CompressionMethod method);

CompressedPayload compress(
    std::span<const std::uint8_t> original,
    CompressionMode mode
);

std::vector<std::uint8_t> decompress(
    std::span<const std::uint8_t> payload,
    CompressionMethod method,
    std::uint64_t expected_size
);

}
