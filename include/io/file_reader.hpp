#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bardzip {

// (64 MiB).
inline constexpr std::uintmax_t max_input_file_size = 64ULL * 1024 * 1024;

std::string read_text_file(const std::filesystem::path& path);

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path);

}
