#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "compression/compression.hpp"
#include "container/container.hpp"

namespace bardzip {

enum class Command {
    Encode,
    Decode,
    Inspect,
    Verify,
    Preview,
    Estimate,
    Identify,
    Completion,
    Help,
    Version
};

// How much the program tells: only errors and results (Quiet), the usual short
// report (Normal), or more detail such as timings (Verbose).
enum class Verbosity {
    Quiet,
    Normal,
    Verbose
};

inline constexpr std::uint64_t default_preview_stanzas = 6;
inline constexpr std::uint64_t max_preview_stanzas = 10000;

struct Arguments {
    Command command;
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;

    CompressionMode compression = CompressionMode::Auto;

    std::uint64_t max_output = default_max_output;
    std::uint64_t max_input = default_max_input;

    bool force = false;   // encode, decode: replace an existing output file
    bool quick = false;   // estimate: size from the file size only, no reading
    std::uint64_t stanzas = default_preview_stanzas;  // preview

    Verbosity verbosity = Verbosity::Normal;
    std::string shell;  // completion: the shell to generate the script for
};

Arguments parse_arguments(int argc, char* argv[]);

// Text printed by `bardzip --help` and `bardzip --version`.
std::string help_text();
std::string version_text();

}
