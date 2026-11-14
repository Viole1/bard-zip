#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "io/byte_sink.hpp"

namespace bardzip {

enum class CompressionMethod : std::uint8_t {
    None = 0,
    Zstd = 1
};

enum class CompressionMode {
    None,
    Zstd,
    Auto
};

struct CompressedPayload {
    CompressionMethod method = CompressionMethod::None;
    std::vector<std::uint8_t> bytes;
};

std::optional<CompressionMethod> compression_method_from_byte(std::uint8_t value);
std::string_view to_string(CompressionMethod method);

CompressedPayload compress(
    std::span<const std::uint8_t> original,
    CompressionMode mode
);

std::vector<std::uint8_t> decompress(
    std::span<const std::uint8_t> payload,
    CompressionMethod method,
    std::uint64_t expected_size
);

// Streaming zstd compression: write() any number of chunks, then finish().
// Compressed bytes are handed to the sink as they are produced.
// source_size is the exact total size that will be written.
class ZstdCompressor {
public:
    ZstdCompressor(std::uint64_t source_size, ByteSink sink);
    ~ZstdCompressor();

    ZstdCompressor(const ZstdCompressor&) = delete;
    ZstdCompressor& operator=(const ZstdCompressor&) = delete;

    void write(std::span<const std::uint8_t> bytes);
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Streaming zstd decompression of exactly one frame. Output never exceeds
// expected_size; finish() verifies that the frame ended and the size matches.
class ZstdDecompressor {
public:
    ZstdDecompressor(std::uint64_t expected_size, ByteSink sink);
    ~ZstdDecompressor();

    ZstdDecompressor(const ZstdDecompressor&) = delete;
    ZstdDecompressor& operator=(const ZstdDecompressor&) = delete;

    void write(std::span<const std::uint8_t> bytes);
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
