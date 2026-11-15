#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace bardzip {

struct FileType {
    std::string_view description;
    std::string_view extension;  // without the dot; empty if there is no typical one
};

// How many leading bytes detect_file_type() needs to see (more are ignored).
inline constexpr std::size_t file_type_sample_size = 32;

// Guesses the type of a file from its first bytes ("magic numbers"). It is a
// guess: a few formats share a signature (a .docx or .jar is a ZIP archive) and
// a file can start like one format while being something else.
FileType detect_file_type(std::span<const std::uint8_t> head);

}
