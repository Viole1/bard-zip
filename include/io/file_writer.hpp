#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace bardzip {

void write_text_file(
    const std::filesystem::path& destination,
    std::string_view text
);

void write_binary_file(
    const std::filesystem::path& destination,
    std::span<const std::uint8_t> bytes
);

}
