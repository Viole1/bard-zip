// libFuzzer target: container extraction with a VALID header (sizes and
// checksums computed from the input), so that the payload handling, zstd
// decompression and the final size / CRC checks are really exercised.
//
// Input layout: [0] flags (bit 0: zstd), [1..2] declared original size,
// [3..6] original CRC32, the rest is the payload.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <vector>

#include "container/container.hpp"
#include "container/crc32.hpp"
#include "container/header.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 7) {
        return 0;
    }

    const std::span<const std::uint8_t> payload{ data + 7, size - 7 };

    bardzip::Header header;
    header.compression = (data[0] & 1) != 0
        ? bardzip::CompressionMethod::Zstd
        : bardzip::CompressionMethod::None;
    header.original_size = static_cast<std::uint64_t>(data[1]) | (static_cast<std::uint64_t>(data[2]) << 8);
    header.payload_size = payload.size();
    header.payload_crc32 = bardzip::crc32(payload);
    header.original_crc32 = static_cast<std::uint32_t>(data[3])
        | (static_cast<std::uint32_t>(data[4]) << 8)
        | (static_cast<std::uint32_t>(data[5]) << 16)
        | (static_cast<std::uint32_t>(data[6]) << 24);

    try {
        const auto header_bytes = bardzip::serialize_header(header);

        std::vector<std::uint8_t> container(header_bytes.begin(), header_bytes.end());
        container.insert(container.end(), payload.begin(), payload.end());

        bardzip::extract_container(container, 1u << 20);
    }
    catch (const std::exception&) {
    }

    return 0;
}
