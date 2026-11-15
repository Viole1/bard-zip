#include "container/container.hpp"

#include <limits>
#include <stdexcept>
#include <string>

#include "container/crc32.hpp"
#include "core/errors.hpp"
#include "container/header.hpp"

namespace bardzip {

void ensure_within_output_limit(
    std::uint64_t declared_size,
    std::uint64_t max_output
) {
    if (declared_size > max_output) {
        throw LimitError(
            "Output limit exceeded: declared "
            + std::to_string(declared_size)
            + " bytes, allowed "
            + std::to_string(max_output)
            + ". Use --max-output <bytes> to raise the limit"
        );
    }
}

std::vector<std::uint8_t> build_container(
    std::span<const std::uint8_t> original,
    CompressionMode mode
) {
    const auto payload = compress(original, mode);

    if (payload.bytes.size() > std::numeric_limits<std::size_t>::max() - header_size) {
        throw std::overflow_error(
            "Container size exceeds maximum allowed size"
        );
    }

    Header header;
    header.compression = payload.method;
    header.original_size = static_cast<std::uint64_t>(original.size());
    header.payload_size = static_cast<std::uint64_t>(payload.bytes.size());
    header.original_crc32 = crc32(original);
    header.payload_crc32 = crc32(payload.bytes);

    const auto header_bytes = serialize_header(header);

    std::vector<std::uint8_t> container;
    container.reserve(header_size + payload.bytes.size());

    container.insert(container.end(), header_bytes.begin(), header_bytes.end());
    container.insert(container.end(), payload.bytes.begin(), payload.bytes.end());

    return container;
}

std::vector<std::uint8_t> extract_container(
    std::span<const std::uint8_t> container,
    std::uint64_t max_output
) {
    if (container.size() < header_size) {
        throw std::invalid_argument("Container too small");
    }

    const auto header = parse_header(container.first(header_size));

    ensure_within_output_limit(header.original_size, max_output);

    if (header.payload_size != container.size() - header_size) {
        throw std::invalid_argument("Container size mismatch");
    }

    const auto payload = container.subspan(header_size);

    if (crc32(payload) != header.payload_crc32) {
        throw std::invalid_argument("Payload CRC mismatch");
    }

    auto original = decompress(payload, header.compression, header.original_size);

    if (crc32(original) != header.original_crc32) {
        throw std::invalid_argument("Original CRC mismatch");
    }

    return original;
}

}
