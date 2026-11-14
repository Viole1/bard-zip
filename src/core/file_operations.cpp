#include "core/file_operations.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "codec/poem_codec.hpp"
#include "compression/compression.hpp"
#include "container/container.hpp"
#include "container/crc32.hpp"
#include "container/header.hpp"
#include "io/byte_sink.hpp"
#include "io/file_reader.hpp"
#include "io/temporary_file.hpp"

namespace bardzip {

namespace {

constexpr const char* input_changed_message =
    "Input file changed while it was being encoded";

constexpr const char* output_exists_message = "Output file already exists";

void require_new_output(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        throw std::runtime_error(output_exists_message);
    }
}

// Measured: a poem is about 53.7 bytes per container byte for random data.
constexpr std::uint64_t poem_bytes_per_byte_estimate = 54;

std::string format_size(std::uint64_t bytes) {
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = mib * 1024.0;

    std::ostringstream text;
    text << std::fixed << std::setprecision(1);

    if (bytes >= gib) {
        text << static_cast<double>(bytes) / gib << " GiB";
    }
    else {
        text << static_cast<double>(bytes) / mib << " MiB";
    }

    return text.str();
}

void ensure_input_within_limit(std::uint64_t size, std::uint64_t max_input) {
    if (max_input == 0 || size <= max_input) {
        return;
    }

    throw std::runtime_error(
        "Input limit exceeded: file is " + std::to_string(size)
        + " bytes, allowed " + std::to_string(max_input)
        + ". The poem would take roughly "
        + format_size(size * poem_bytes_per_byte_estimate)
        + " of disk space. Use --max-input <bytes> to raise the limit (0 = no limit)"
    );
}

std::ofstream open_scratch_output(const ScratchFile& scratch) {
    std::ofstream output(scratch.path(), std::ios::binary | std::ios::trunc);

    if (!output) {
        throw std::runtime_error("Failed to open temporary file for writing");
    }

    return output;
}

void write_scratch(std::ofstream& output, std::span<const std::uint8_t> bytes) {
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!output) {
        throw std::runtime_error("Failed to write temporary file");
    }
}

void close_scratch(std::ofstream& output) {
    output.close();

    if (!output) {
        throw std::runtime_error("Failed to finish temporary file");
    }
}

std::uint64_t stanza_count_for(std::uint64_t payload_size) {
    const auto container_size = header_size + payload_size;
    return container_size / 2 + container_size % 2;
}

// Reads the poem and rebuilds the original bytes without holding either in
// memory: poem -> scratch payload (CRC checked) -> decompressed original.
// Nothing reaches the sink before the header and the payload CRC are valid.
void extract_poem_stream(
    std::istream& poem,
    std::uint64_t max_output,
    const std::filesystem::path& scratch_base,
    const ByteSink& sink
) {
    const ScratchFile scratch(scratch_base);

    Crc32 payload_crc;
    Header header;

    {
        auto payload_out = open_scratch_output(scratch);

        header = decode_poem_stream(
            poem,
            [&](const Header& parsed) {
                ensure_within_output_limit(parsed.original_size, max_output);
            },
            [&](std::span<const std::uint8_t> chunk) {
                payload_crc.update(chunk);
                write_scratch(payload_out, chunk);
            }
        );

        close_scratch(payload_out);
    }

    if (payload_crc.value() != header.payload_crc32) {
        throw std::invalid_argument("Payload CRC mismatch");
    }

    Crc32 original_crc;
    std::uint64_t original_size = 0;

    const ByteSink checked_sink = [&](std::span<const std::uint8_t> chunk) {
        original_crc.update(chunk);
        original_size += chunk.size();
        sink(chunk);
    };

    std::optional<ZstdDecompressor> decompressor;

    if (header.compression == CompressionMethod::Zstd) {
        decompressor.emplace(header.original_size, checked_sink);
    }

    auto payload_in = open_input_file(scratch.path());
    std::vector<std::uint8_t> block(stream_block_size);

    while (const auto count = read_block(payload_in, block)) {
        const std::span<const std::uint8_t> chunk{ block.data(), count };

        if (decompressor) {
            decompressor->write(chunk);
        }
        else {
            checked_sink(chunk);
        }
    }

    if (decompressor) {
        decompressor->finish();
    }

    if (original_size != header.original_size) {
        throw std::invalid_argument(
            "Decompressed size does not match original size"
        );
    }

    if (original_crc.value() != header.original_crc32) {
        throw std::invalid_argument("Original CRC mismatch");
    }
}

struct PayloadPlan {
    CompressionMethod method = CompressionMethod::None;
    std::uint64_t original_size = 0;
    std::uint32_t original_crc32 = 0;
    std::uint64_t payload_size = 0;
    std::uint32_t payload_crc32 = 0;
};

}

EncodeReport encode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    CompressionMode mode,
    std::uint64_t max_input
) {
    require_new_output(output_path);

    auto input = open_input_file(input_path);
    const auto declared_size = std::filesystem::file_size(input_path);

    ensure_input_within_limit(declared_size, max_input);

    // Pass 1: measure the original and, if requested, compress it into a
    // scratch file. The header needs both sizes and CRCs before the payload.
    const bool try_compress =
        mode == CompressionMode::Zstd ||
        (mode == CompressionMode::Auto && declared_size != 0);

    std::optional<ScratchFile> scratch;
    std::ofstream scratch_out;
    std::optional<ZstdCompressor> compressor;

    PayloadPlan plan;
    Crc32 original_crc;
    Crc32 payload_crc;

    if (try_compress) {
        scratch.emplace(output_path);
        scratch_out = open_scratch_output(*scratch);

        compressor.emplace(
            declared_size,
            [&](std::span<const std::uint8_t> chunk) {
                payload_crc.update(chunk);
                plan.payload_size += chunk.size();
                write_scratch(scratch_out, chunk);
            }
        );
    }

    std::vector<std::uint8_t> block(stream_block_size);

    while (const auto count = read_block(input, block)) {
        if (count > declared_size - plan.original_size) {
            throw std::runtime_error(input_changed_message);
        }

        const std::span<const std::uint8_t> chunk{ block.data(), count };

        original_crc.update(chunk);
        plan.original_size += count;

        if (compressor) {
            compressor->write(chunk);
        }
    }

    if (plan.original_size != declared_size) {
        throw std::runtime_error(input_changed_message);
    }

    plan.original_crc32 = original_crc.value();

    if (compressor) {
        compressor->finish();
        close_scratch(scratch_out);
    }

    const bool keep_compressed = compressor &&
        !(mode == CompressionMode::Auto && plan.payload_size >= plan.original_size);

    if (keep_compressed) {
        plan.method = CompressionMethod::Zstd;
        plan.payload_crc32 = payload_crc.value();
    }
    else {
        // Stored: the payload is the original itself, re-read in pass 2.
        plan.method = CompressionMethod::None;
        plan.payload_size = plan.original_size;
        plan.payload_crc32 = plan.original_crc32;
        scratch.reset();
    }

    Header header;
    header.compression = plan.method;
    header.original_size = plan.original_size;
    header.payload_size = plan.payload_size;
    header.original_crc32 = plan.original_crc32;
    header.payload_crc32 = plan.payload_crc32;

    // Pass 2: header + payload -> poem, written to a temporary file that is
    // published only when everything has succeeded.
    TemporaryFile output(output_path);

    PoemEncoder poem([&](std::string_view text) {
        output.write({
            reinterpret_cast<const std::uint8_t*>(text.data()),
            text.size()
        });
    });

    poem.write(serialize_header(header));

    std::uint64_t streamed = 0;
    Crc32 streamed_crc;

    const auto source_path = keep_compressed ? scratch->path() : input_path;
    auto source = open_input_file(source_path);

    while (const auto count = read_block(source, block)) {
        const std::span<const std::uint8_t> chunk{ block.data(), count };

        streamed += count;
        streamed_crc.update(chunk);
        poem.write(chunk);
    }

    if (streamed != plan.payload_size || streamed_crc.value() != plan.payload_crc32) {
        throw std::runtime_error(
            keep_compressed ? "Temporary payload was modified" : input_changed_message
        );
    }

    poem.finish();
    output.commit();

    return {
        plan.method,
        plan.original_size,
        plan.payload_size,
        static_cast<std::size_t>(header_size + plan.payload_size),
        poem.text_size(),
        poem.stanza_count()
    };
}

void decode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    std::uint64_t max_output
) {
    require_new_output(output_path);

    auto poem = open_input_file(input_path);
    TemporaryFile output(output_path);

    extract_poem_stream(
        poem,
        max_output,
        output_path,
        [&](std::span<const std::uint8_t> chunk) { output.write(chunk); }
    );

    output.commit();
}

void inspect_file(const std::filesystem::path& input_path) {
    auto poem = open_input_file(input_path);

    const auto header = decode_poem_stream(
        poem,
        {},
        [](std::span<const std::uint8_t>) {}
    );

    std::cout << "bard-zip container\n";
    std::cout << "--------------------------\n";

    std::cout << "Version:        "
        << static_cast<int>(header.version) << '\n';

    std::cout << "Compression:    " << to_string(header.compression) << '\n';

    std::cout << "Original size:  " << header.original_size << " bytes\n";
    std::cout << "Payload size:   " << header.payload_size << " bytes\n";

    std::cout << "Original CRC32: 0x"
        << std::hex << std::setw(8) << std::setfill('0')
        << header.original_crc32 << std::dec << '\n';

    std::cout << "Payload CRC32:  0x"
        << std::hex << std::setw(8) << std::setfill('0')
        << header.payload_crc32 << std::dec << '\n';

    std::cout << "Stanzas:        " << stanza_count_for(header.payload_size) << '\n';

    std::cout << "Payload integrity: not checked\n";
}

void verify_file(
    const std::filesystem::path& input_path,
    std::uint64_t max_output
) {
    auto poem = open_input_file(input_path);

    extract_poem_stream(
        poem,
        max_output,
        std::filesystem::temp_directory_path() / "bardzip-verify",
        [](std::span<const std::uint8_t>) {}
    );

    std::cout << "Verification successful.\n";
}

}
