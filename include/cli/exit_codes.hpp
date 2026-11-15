#pragma once

#include <exception>
#include <string_view>

namespace bardzip {

// Process exit codes, documented in `bardzip --help` and docs/*/cli.md.
enum class ExitCode : int {
    Success = 0,
    Internal = 1,   // a bug or an unexpected failure
    Usage = 2,      // wrong command line
    System = 3,     // a file or system problem: cannot open, cannot write, output exists, disk full
    Data = 4,       // bad data: not a poem, damaged, checksum or size mismatch, unsupported version
    Limit = 5       // --max-input / --max-output exceeded
};

ExitCode exit_code_for(const std::exception& error) noexcept;

std::string_view describe(ExitCode code) noexcept;

}
