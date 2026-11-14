#include "compression/compression.hpp"

#include <zstd.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace bardzip {

namespace {

constexpr int zstd_level = 3;
constexpr int max_zstd_window_log = 24;

struct CompressionContextDeleter {
    void operator()(ZSTD_CCtx* context) const noexcept {
        ZSTD_freeCCtx(context);
    }
};

struct DecompressionContextDeleter {
    void operator()(ZSTD_DCtx* context) const noexcept {
        ZSTD_freeDCtx(context);
    }
};

}

struct ZstdCompressor::Impl {
    std::unique_ptr<ZSTD_CCtx, CompressionContextDeleter> context{ ZSTD_createCCtx() };
    ByteSink sink;
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(stream_block_size);

    void check(std::size_t result) const {
        if (ZSTD_isError(result)) {
            throw std::runtime_error(
                std::string("zstd compression failed: ") + ZSTD_getErrorName(result)
            );
        }
    }

    void drive(ZSTD_EndDirective directive, ZSTD_inBuffer& input) {
        while (true) {
            ZSTD_outBuffer output{ buffer.data(), buffer.size(), 0 };

            const auto remaining =
                ZSTD_compressStream2(context.get(), &output, &input, directive);
            check(remaining);

            if (output.pos != 0) {
                sink({ buffer.data(), output.pos });
            }

            const bool done = directive == ZSTD_e_end
                ? remaining == 0
                : input.pos == input.size;

            if (done) {
                return;
            }
        }
    }
};

ZstdCompressor::ZstdCompressor(std::uint64_t source_size, ByteSink sink)
    : impl_(std::make_unique<Impl>())
{
    if (!impl_->context) {
        throw std::runtime_error("Failed to create zstd compression context");
    }

    impl_->sink = std::move(sink);

    impl_->check(ZSTD_CCtx_setParameter(
        impl_->context.get(), ZSTD_c_compressionLevel, zstd_level));
    impl_->check(ZSTD_CCtx_setPledgedSrcSize(impl_->context.get(), source_size));
}

ZstdCompressor::~ZstdCompressor() = default;

void ZstdCompressor::write(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return;
    }

    ZSTD_inBuffer input{ bytes.data(), bytes.size(), 0 };
    impl_->drive(ZSTD_e_continue, input);
}

void ZstdCompressor::finish() {
    ZSTD_inBuffer input{ nullptr, 0, 0 };
    impl_->drive(ZSTD_e_end, input);
}

struct ZstdDecompressor::Impl {
    std::unique_ptr<ZSTD_DCtx, DecompressionContextDeleter> context{ ZSTD_createDCtx() };
    ByteSink sink;
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(stream_block_size);
    std::uint64_t expected_size = 0;
    std::uint64_t produced = 0;
    bool frame_done = false;
};

ZstdDecompressor::ZstdDecompressor(std::uint64_t expected_size, ByteSink sink)
    : impl_(std::make_unique<Impl>())
{
    if (!impl_->context) {
        throw std::runtime_error("Failed to create zstd decompression context");
    }

    impl_->sink = std::move(sink);
    impl_->expected_size = expected_size;

    // Our encoder never uses a window above 2^21 bytes. A hostile frame may
    // ask for up to 2^27 (128 MiB) of window memory; refuse anything above 16 MiB.
    const auto limit = ZSTD_DCtx_setParameter(
        impl_->context.get(), ZSTD_d_windowLogMax, max_zstd_window_log);

    if (ZSTD_isError(limit)) {
        throw std::runtime_error("Failed to configure the zstd decompression context");
    }
}

ZstdDecompressor::~ZstdDecompressor() = default;

void ZstdDecompressor::write(std::span<const std::uint8_t> bytes) {
    auto& state = *impl_;

    if (bytes.empty()) {
        return;
    }

    if (state.frame_done) {
        throw std::invalid_argument("Unexpected data after zstd frame");
    }

    ZSTD_inBuffer input{ bytes.data(), bytes.size(), 0 };

    while (true) {
        ZSTD_outBuffer output{ state.buffer.data(), state.buffer.size(), 0 };
        const auto input_before = input.pos;

        const auto hint = ZSTD_decompressStream(state.context.get(), &output, &input);

        if (ZSTD_isError(hint)) {
            throw std::invalid_argument(
                std::string("Corrupted zstd payload: ") + ZSTD_getErrorName(hint)
            );
        }

        if (output.pos > state.expected_size - state.produced) {
            throw std::invalid_argument(
                "Decompressed data exceeds declared original size"
            );
        }

        if (output.pos != 0) {
            state.produced += output.pos;
            state.sink({ state.buffer.data(), output.pos });
        }

        if (hint == 0) {
            state.frame_done = true;

            if (input.pos != input.size) {
                throw std::invalid_argument("Unexpected data after zstd frame");
            }

            return;
        }

        const bool input_consumed = input.pos == input.size;
        const bool output_has_room = output.pos < output.size;

        // The decoder may still hold buffered output after taking all input.
        if (input_consumed && output_has_room) {
            return;
        }

        if (output.pos == 0 && input.pos == input_before) {
            throw std::invalid_argument("Corrupted zstd payload: no progress");
        }
    }
}

void ZstdDecompressor::finish() {
    auto& state = *impl_;

    if (!state.frame_done) {
        throw std::invalid_argument("Truncated zstd payload");
    }

    if (state.produced != state.expected_size) {
        throw std::invalid_argument(
            "Decompressed size does not match original size"
        );
    }
}

namespace {

std::vector<std::uint8_t> compress_zstd(std::span<const std::uint8_t> original) {
    std::vector<std::uint8_t> compressed;

    ZstdCompressor compressor(original.size(), [&](std::span<const std::uint8_t> chunk) {
        compressed.insert(compressed.end(), chunk.begin(), chunk.end());
    });

    compressor.write(original);
    compressor.finish();

    return compressed;
}

std::vector<std::uint8_t> decompress_zstd(
    std::span<const std::uint8_t> payload,
    std::uint64_t expected_size
) {
    std::vector<std::uint8_t> result;

    ZstdDecompressor decompressor(expected_size, [&](std::span<const std::uint8_t> chunk) {
        result.insert(result.end(), chunk.begin(), chunk.end());
    });

    decompressor.write(payload);
    decompressor.finish();

    return result;
}

}

std::optional<CompressionMethod> compression_method_from_byte(std::uint8_t value) {
    switch (value) {
    case static_cast<std::uint8_t>(CompressionMethod::None):
        return CompressionMethod::None;
    case static_cast<std::uint8_t>(CompressionMethod::Zstd):
        return CompressionMethod::Zstd;
    default:
        return std::nullopt;
    }
}

std::string_view to_string(CompressionMethod method) {
    switch (method) {
    case CompressionMethod::None:
        return "none";
    case CompressionMethod::Zstd:
        return "zstd";
    }

    return "unknown";
}

CompressedPayload compress(
    std::span<const std::uint8_t> original,
    CompressionMode mode
) {
    const auto stored = [&] {
        return CompressedPayload{
            CompressionMethod::None,
            { original.begin(), original.end() }
        };
    };

    if (mode == CompressionMode::None ||
        (mode == CompressionMode::Auto && original.empty()))
    {
        return stored();
    }

    auto compressed = compress_zstd(original);

    if (mode == CompressionMode::Auto && compressed.size() >= original.size()) {
        return stored();
    }

    return { CompressionMethod::Zstd, std::move(compressed) };
}

std::vector<std::uint8_t> decompress(
    std::span<const std::uint8_t> payload,
    CompressionMethod method,
    std::uint64_t expected_size
) {
    switch (method) {
    case CompressionMethod::None:
        if (payload.size() != expected_size) {
            throw std::invalid_argument(
                "Stored payload size does not match original size"
            );
        }
        return { payload.begin(), payload.end() };

    case CompressionMethod::Zstd:
        return decompress_zstd(payload, expected_size);
    }

    throw std::invalid_argument("Unsupported compression method");
}

}
