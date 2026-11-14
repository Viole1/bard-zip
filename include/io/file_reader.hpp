#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>

namespace bardzip {

// Opens a file for binary reading; throws if it cannot be opened.
std::ifstream open_input_file(const std::filesystem::path& path);

// Reads up to buffer.size() bytes; returns the number of bytes read
// (less than buffer.size() only at the end of the stream, 0 at EOF).
std::size_t read_block(std::istream& input, std::span<std::uint8_t> buffer);

}
