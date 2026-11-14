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
    Help,
    Version
};

struct Arguments {
    Command command;
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;

    CompressionMode compression = CompressionMode::Auto;

    std::uint64_t max_output = default_max_output;
    std::uint64_t max_input = default_max_input;
};

Arguments parse_arguments(int argc, char* argv[]);

// Text printed by `bardzip --help` and `bardzip --version`.
std::string help_text();
std::string version_text();

}
