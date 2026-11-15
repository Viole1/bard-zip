#include "core/file_operations.hpp"

#include <algorithm>
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
#include "core/errors.hpp"
#include "core/size_format.hpp"
#include "detect/file_type.hpp"
#include "io/byte_sink.hpp"
#include "io/file_reader.hpp"
#include "io/prefetch_stream.hpp"
#include "io/stdio.hpp"
#include "io/temporary_file.hpp"

namespace bardzip {

namespace {

// A poem file read ahead by a background thread, so disk reads overlap with
// decoding. Member order matters: the thread (in `buffer_`) must stop before
// the file it reads is closed.
class PoemInput {
public:
    // "-" reads standard input.
    explicit PoemInput(const std::filesystem::path& path)
        : source_(open_input_stream(path)), buffer_(source_->stream()), stream_(&buffer_)
    {
    }

    std::istream& stream() { return stream_; }

private:
    std::unique_ptr<InputStream> source_;
    PrefetchStreambuf buffer_;
    std::istream stream_;
};

constexpr const char* input_changed_message =
    "Input file changed while it was being encoded";

constexpr const char* output_exists_message =
    "Output file already exists. Use --force to replace it";

// An existing output is refused unless overwriting was asked for, and the
// output may never be the input itself (the input is still being read).
void prepare_output(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    bool overwrite
) {
    // Standard output has nothing to protect.
    if (is_stdio(output_path)) {
        return;
    }

    std::error_code error;

    if (!std::filesystem::exists(output_path, error)) {
        return;
    }

    if (!is_stdio(input_path) && std::filesystem::equivalent(input_path, output_path, error)) {
        throw std::runtime_error("The output file is the same as the input file");
    }

    if (!overwrite) {
        throw std::runtime_error(output_exists_message);
    }
}

// Measured: a poem is about 53.7 bytes per container byte for random data.
constexpr std::uint64_t poem_bytes_per_byte_estimate = 54;

// Auto mode keeps zstd only if it makes the payload smaller.
bool should_keep_compressed(
    CompressionMode mode,
    std::uint64_t compressed_size,
    std::uint64_t original_size
) {
    return !(mode == CompressionMode::Auto && compressed_size >= original_size);
}

bool should_try_compression(CompressionMode mode, std::uint64_t original_size) {
    return mode == CompressionMode::Zstd ||
        (mode == CompressionMode::Auto && original_size != 0);
}

void ensure_input_within_limit(std::uint64_t size, std::uint64_t max_input) {
    if (max_input == 0 || size <= max_input) {
        return;
    }

    throw LimitError(
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

// Where a result goes: a file, published atomically on commit(), or standard
// output (a pipe: there is nothing to publish).
class OutputTarget {
public:
    virtual ~OutputTarget() = default;
    virtual void write(std::span<const std::uint8_t> bytes) = 0;
    virtual void commit() = 0;
};

class FileOutput final : public OutputTarget {
public:
    FileOutput(const std::filesystem::path& path, bool overwrite) : file_(path, overwrite) {}

    void write(std::span<const std::uint8_t> bytes) override { file_.write(bytes); }
    void commit() override { file_.commit(); }

private:
    TemporaryFile file_;
};

class StdoutOutput final : public OutputTarget {
public:
    void write(std::span<const std::uint8_t> bytes) override { write_stdout(bytes); }
    void commit() override { flush_stdout(); }
};

std::unique_ptr<OutputTarget> open_output(const std::filesystem::path& path, bool overwrite) {
    if (is_stdio(path)) {
        return std::make_unique<StdoutOutput>();
    }

    return std::make_unique<FileOutput>(path, overwrite);
}

// Work files are created next to the output file; for standard output they go
// to the temporary folder.
std::filesystem::path scratch_base_for(const std::filesystem::path& output_path) {
    return is_stdio(output_path)
        ? std::filesystem::temp_directory_path() / "bardzip-output"
        : output_path;
}

// The input of encode and analyze: a file, or standard input copied to a work
// file first (they read their input twice and need its exact size up front).
// With a non-zero max_input, receiving stops with an error once it is exceeded.
class EncodeInput {
public:
    EncodeInput(
        const std::filesystem::path& input,
        std::uint64_t max_input,
        ProgressObserver* progress
    ) {
        if (!is_stdio(input)) {
            path_ = input;
            return;
        }

        spool_.emplace(std::filesystem::temp_directory_path() / "bardzip-input");
        path_ = spool_->path();

        const auto source = open_input_stream(input);
        auto out = open_scratch_output(*spool_);
        const ProgressStage receiving(progress, "Receiving input", 0);

        std::vector<std::uint8_t> block(stream_block_size);
        std::uint64_t received = 0;

        while (const auto count = read_block(source->stream(), block)) {
            received += count;

            if (max_input != 0 && received > max_input) {
                throw LimitError(
                    "Input limit exceeded: more than " + std::to_string(max_input)
                    + " bytes arrived on standard input. Use --max-input <bytes> to raise the limit (0 = no limit)"
                );
            }

            write_scratch(out, { block.data(), count });
            receiving.advance(received);
        }

        close_scratch(out);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::optional<ScratchFile> spool_;
};

std::uint64_t stanza_count_for(std::uint64_t payload_size) {
    const auto container_size = header_size + payload_size;
    return container_size / 2 + container_size % 2;
}

// Reads the poem and rebuilds the original bytes without holding either in
// memory: poem -> scratch payload (CRC checked) -> decompressed original.
// Nothing reaches the sink before the header and the payload CRC are valid.
Header extract_poem_stream(
    std::istream& poem,
    std::uint64_t max_output,
    const std::filesystem::path& scratch_base,
    const ByteSink& sink,
    ProgressObserver* progress
) {
    const ScratchFile scratch(scratch_base);

    Crc32 payload_crc;
    Header header;

    {
        auto payload_out = open_scratch_output(scratch);

        // The stage starts once the header is known: it tells how much follows.
        std::optional<ProgressStage> reading;
        std::uint64_t received = 0;

        header = decode_poem_stream(
            poem,
            [&](const Header& parsed) {
                ensure_within_output_limit(parsed.original_size, max_output);
                reading.emplace(progress, "Reading poem", parsed.payload_size);
            },
            [&](std::span<const std::uint8_t> chunk) {
                payload_crc.update(chunk);
                write_scratch(payload_out, chunk);

                received += chunk.size();

                if (reading) {
                    reading->advance(received);
                }
            }
        );

        reading.reset();
        close_scratch(payload_out);
    }

    if (payload_crc.value() != header.payload_crc32) {
        throw std::invalid_argument("Payload CRC mismatch");
    }

    const ProgressStage restoring(progress, "Restoring file", header.original_size);

    Crc32 original_crc;
    std::uint64_t original_size = 0;

    const ByteSink checked_sink = [&](std::span<const std::uint8_t> chunk) {
        original_crc.update(chunk);
        original_size += chunk.size();
        sink(chunk);
        restoring.advance(original_size, original_size);
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

    return header;
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
    std::uint64_t max_input,
    bool overwrite,
    ProgressObserver* progress
) {
    prepare_output(input_path, output_path, overwrite);

    const EncodeInput source(input_path, max_input, progress);
    auto input = open_input_file(source.path());
    const auto declared_size = std::filesystem::file_size(source.path());

    ensure_input_within_limit(declared_size, max_input);

    // Pass 1: measure the original and, if requested, compress it into a
    // scratch file. The header needs both sizes and CRCs before the payload.
    const bool try_compress = should_try_compression(mode, declared_size);

    std::optional<ScratchFile> scratch;
    std::ofstream scratch_out;
    std::optional<ZstdCompressor> compressor;

    PayloadPlan plan;
    Crc32 original_crc;
    Crc32 payload_crc;

    if (try_compress) {
        scratch.emplace(scratch_base_for(output_path));
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

    {
        const ProgressStage reading(progress, "Reading", declared_size);

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

            reading.advance(plan.original_size);
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
        should_keep_compressed(mode, plan.payload_size, plan.original_size);

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
    // published only when everything has succeeded (or to standard output).
    const auto output = open_output(output_path, overwrite);

    PoemEncoder poem([&](std::string_view text) {
        output->write({
            reinterpret_cast<const std::uint8_t*>(text.data()),
            text.size()
        });
    });

    poem.write(serialize_header(header));

    std::uint64_t streamed = 0;
    Crc32 streamed_crc;

    const auto source_path = keep_compressed ? scratch->path() : source.path();
    auto payload = open_input_file(source_path);

    {
        const ProgressStage writing(progress, "Writing poem", plan.payload_size);

        while (const auto count = read_block(payload, block)) {
            const std::span<const std::uint8_t> chunk{ block.data(), count };

            streamed += count;
            streamed_crc.update(chunk);
            poem.write(chunk);

            writing.advance(streamed, poem.text_size());
        }
    }

    if (streamed != plan.payload_size || streamed_crc.value() != plan.payload_crc32) {
        throw std::runtime_error(
            keep_compressed ? "Temporary payload was modified" : input_changed_message
        );
    }

    poem.finish();
    output->commit();

    return {
        plan.method,
        plan.original_size,
        plan.payload_size,
        static_cast<std::size_t>(header_size + plan.payload_size),
        poem.text_size(),
        poem.stanza_count()
    };
}

DecodeReport decode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    std::uint64_t max_output,
    bool overwrite,
    ProgressObserver* progress
) {
    prepare_output(input_path, output_path, overwrite);

    if (is_stdio(output_path) && is_console(1)) {
        throw UsageError(
            "Refusing to write binary data to the terminal. Redirect the output "
            "(> file, or | another program) or use -o <file>"
        );
    }

    PoemInput input(input_path);
    auto& poem = input.stream();
    const auto output = open_output(output_path, overwrite);

    const auto header = extract_poem_stream(
        poem,
        max_output,
        scratch_base_for(output_path),
        [&](std::span<const std::uint8_t> chunk) { output->write(chunk); },
        progress
    );

    output->commit();

    return { header.compression, header.original_size, header.payload_size };
}

void inspect_file(const std::filesystem::path& input_path) {
    PoemInput input(input_path);
    auto& poem = input.stream();

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

DecodeReport verify_file(
    const std::filesystem::path& input_path,
    std::uint64_t max_output,
    ProgressObserver* progress
) {
    PoemInput input(input_path);
    auto& poem = input.stream();

    const auto header = extract_poem_stream(
        poem,
        max_output,
        std::filesystem::temp_directory_path() / "bardzip-verify",
        [](std::span<const std::uint8_t>) {},
        progress
    );

    return { header.compression, header.original_size, header.payload_size };
}

namespace {

// Appends at most `limit - head.size()` bytes of `chunk` to `head`.
void append_head(
    std::vector<std::uint8_t>& head,
    std::span<const std::uint8_t> chunk,
    std::size_t limit
) {
    if (head.size() < limit) {
        const auto take = (std::min)(chunk.size(), limit - head.size());
        head.insert(head.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(take));
    }
}

// Thrown by identify_file once it has seen enough bytes; never leaves the function.
struct EnoughBytes {};

}

PoemAnalysis analyze_file(
    const std::filesystem::path& input_path,
    CompressionMode mode,
    std::size_t head_stanzas,
    ProgressObserver* progress
) {
    const EncodeInput source(input_path, 0, progress);
    auto input = open_input_file(source.path());
    const auto declared_size = std::filesystem::file_size(source.path());

    // The shown part of the container: the header plus the start of the payload.
    const std::size_t head_bytes = head_stanzas * 2;
    const std::size_t payload_head_bytes =
        head_bytes > header_size ? head_bytes - header_size : 0;

    Crc32 original_crc;
    Crc32 payload_crc;
    PoemMeter original_meter;
    PoemMeter compressed_meter;
    std::vector<std::uint8_t> original_head;
    std::vector<std::uint8_t> compressed_head;
    std::uint64_t original_size = 0;
    std::uint64_t compressed_size = 0;

    std::optional<ZstdCompressor> compressor;

    if (should_try_compression(mode, declared_size)) {
        compressor.emplace(
            declared_size,
            [&](std::span<const std::uint8_t> chunk) {
                payload_crc.update(chunk);
                compressed_size += chunk.size();
                compressed_meter.write(chunk);
                append_head(compressed_head, chunk, payload_head_bytes);
            }
        );
    }

    std::vector<std::uint8_t> block(stream_block_size);

    {
        const ProgressStage analyzing(progress, "Analyzing", declared_size);

        while (const auto count = read_block(input, block)) {
            if (count > declared_size - original_size) {
                throw std::runtime_error(input_changed_message);
            }

            const std::span<const std::uint8_t> chunk{ block.data(), count };

            original_crc.update(chunk);
            original_meter.write(chunk);
            append_head(original_head, chunk, payload_head_bytes);
            original_size += count;

            if (compressor) {
                compressor->write(chunk);
            }

            analyzing.advance(original_size);
        }
    }

    if (original_size != declared_size) {
        throw std::runtime_error(input_changed_message);
    }

    original_meter.finish();

    if (compressor) {
        compressor->finish();
        compressed_meter.finish();
    }

    const bool keep_compressed = compressor &&
        should_keep_compressed(mode, compressed_size, original_size);

    Header header;
    header.original_size = original_size;
    header.original_crc32 = original_crc.value();

    if (keep_compressed) {
        header.compression = CompressionMethod::Zstd;
        header.payload_size = compressed_size;
        header.payload_crc32 = payload_crc.value();
    }
    else {
        header.compression = CompressionMethod::None;
        header.payload_size = original_size;
        header.payload_crc32 = header.original_crc32;
    }

    const auto& payload_meter = keep_compressed ? compressed_meter : original_meter;
    const auto& payload_head = keep_compressed ? compressed_head : original_head;

    PoemMeter header_meter;
    const auto header_bytes = serialize_header(header);
    header_meter.write(header_bytes);
    header_meter.finish();

    PoemAnalysis analysis;
    analysis.method = header.compression;
    analysis.original_size = original_size;
    analysis.payload_size = header.payload_size;
    analysis.container_size = header_size + header.payload_size;
    analysis.stanza_count = header_meter.stanza_count() + payload_meter.stanza_count();
    analysis.poem_size = poem_marker_text_size
        + header_meter.text_size() + payload_meter.text_size();

    // The first stanzas, built by the real encoder from the head of the container.
    std::vector<std::uint8_t> container_head(header_bytes.begin(), header_bytes.end());
    container_head.insert(container_head.end(), payload_head.begin(), payload_head.end());
    container_head.resize((std::min)(container_head.size(), head_bytes));

    analysis.head = encode_poem(container_head);

    return analysis;
}

FileTypeGuess identify_file(const std::filesystem::path& poem_path) {
    PoemInput input(poem_path);

    FileTypeGuess guess;
    std::optional<ZstdDecompressor> decompressor;
    Header header;

    const auto collect = [&](std::span<const std::uint8_t> bytes) {
        append_head(guess.first_bytes, bytes, file_type_sample_size);

        if (guess.first_bytes.size() >= file_type_sample_size ||
            guess.first_bytes.size() >= header.original_size)
        {
            throw EnoughBytes{};
        }
    };

    try {
        decode_poem_stream(
            input.stream(),
            [&](const Header& parsed) {
                header = parsed;

                if (parsed.compression == CompressionMethod::Zstd && parsed.original_size != 0) {
                    decompressor.emplace(parsed.original_size, collect);
                }
            },
            [&](std::span<const std::uint8_t> chunk) {
                if (header.original_size == 0) {
                    return;
                }

                if (decompressor) {
                    decompressor->write(chunk);
                }
                else {
                    collect(chunk);
                }
            }
        );
    }
    catch (const EnoughBytes&) {
    }

    guess.original_size = header.original_size;
    guess.method = header.compression;

    const auto type = detect_file_type(guess.first_bytes);
    guess.description = std::string(type.description);
    guess.extension = std::string(type.extension);

    return guess;
}

std::optional<std::uint64_t> free_space_for(const std::filesystem::path& path) {
    std::error_code error;

    auto probe = std::filesystem::absolute(path, error);

    // Walk up to a folder that exists (the output file itself does not yet).
    while (!error && !probe.empty() && !std::filesystem::exists(probe, error)) {
        const auto parent = probe.parent_path();

        if (parent == probe) {
            break;
        }

        probe = parent;
    }

    if (error) {
        return std::nullopt;
    }

    const auto info = std::filesystem::space(probe, error);

    if (error) {
        return std::nullopt;
    }

    return info.available;
}

}
