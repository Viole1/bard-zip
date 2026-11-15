#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "compression/compression.hpp"
#include "container/container.hpp"
#include "core/progress.hpp"

namespace bardzip {

// Where a path names a file, "-" can be given instead for standard input
// (inputs) or standard output (outputs): see io/stdio.hpp. The `progress`
// parameters are optional and receive reports for long operations.

struct EncodeReport {
    CompressionMethod method = CompressionMethod::None;
    std::uint64_t original_size = 0;
    std::uint64_t payload_size = 0;
    std::size_t container_size = 0;
    std::size_t poem_size = 0;
    std::size_t stanza_count = 0;
};

// With overwrite, an existing output file is atomically replaced once the new
// result is complete; if anything fails, the old file stays untouched.
// The output may never be the input file itself. Standard input is first
// copied to a temporary file (the encoder needs the exact size and two passes).
EncodeReport encode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    CompressionMode mode = CompressionMode::Auto,
    std::uint64_t max_input = default_max_input,
    bool overwrite = false,
    ProgressObserver* progress = nullptr
);

// What decode and verify found in a poem.
struct DecodeReport {
    CompressionMethod method = CompressionMethod::None;
    std::uint64_t original_size = 0;
    std::uint64_t payload_size = 0;
};

// Refuses to write binary data to a console when the output is "-".
DecodeReport decode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    std::uint64_t max_output = default_max_output,
    bool overwrite = false,
    ProgressObserver* progress = nullptr
);

void inspect_file(const std::filesystem::path& input_path);

// Checks the whole poem. Nothing is printed.
DecodeReport verify_file(
    const std::filesystem::path& input_path,
    std::uint64_t max_output = default_max_output,
    ProgressObserver* progress = nullptr
);

// What `encode` would produce for a file, worked out without writing anything.
struct PoemAnalysis {
    CompressionMethod method = CompressionMethod::None;
    std::uint64_t original_size = 0;
    std::uint64_t payload_size = 0;
    std::uint64_t container_size = 0;
    std::uint64_t stanza_count = 0;
    std::uint64_t poem_size = 0;  // exact, in bytes
    std::string head;             // the marker and the first stanzas, exactly as encode writes them
};

// Reads the file once (compressing it in memory if the mode asks for it) and
// returns the exact size of the poem plus its first `head_stanzas` stanzas.
// Memory use is bounded; nothing is written to the output. No input size limit applies.
PoemAnalysis analyze_file(
    const std::filesystem::path& input_path,
    CompressionMode mode,
    std::size_t head_stanzas,
    ProgressObserver* progress = nullptr
);

// A guess about the type of the file hidden in a poem.
struct FileTypeGuess {
    std::string description;
    std::string extension;                // without the dot, may be empty
    std::uint64_t original_size = 0;
    CompressionMethod method = CompressionMethod::None;
    std::vector<std::uint8_t> first_bytes;
};

// Looks at the first bytes of the hidden file (decompressing just enough of a
// zstd payload). Reads only the beginning of the poem and does NOT verify it:
// use `verify` for that.
FileTypeGuess identify_file(const std::filesystem::path& poem_path);

// Free disk space on the volume that contains `path` (or its nearest existing
// parent); nullopt if it cannot be determined.
std::optional<std::uint64_t> free_space_for(const std::filesystem::path& path);

}
