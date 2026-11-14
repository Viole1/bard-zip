#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "compression/compression.hpp"
#include "container/container.hpp"

namespace bardzip {

struct EncodeReport {
    CompressionMethod method = CompressionMethod::None;
    std::uint64_t original_size = 0;
    std::uint64_t payload_size = 0;
    std::size_t container_size = 0;
    std::size_t poem_size = 0;
    std::size_t stanza_count = 0;
};

EncodeReport encode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    CompressionMode mode = CompressionMode::Auto,
    std::uint64_t max_input = default_max_input
);

void decode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    std::uint64_t max_output = default_max_output
);

void inspect_file(const std::filesystem::path& input_path);

void verify_file(
    const std::filesystem::path& input_path,
    std::uint64_t max_output = default_max_output
);

}
