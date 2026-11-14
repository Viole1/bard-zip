// libFuzzer target: the 36-byte container header parser. The magic and the
// header CRC are repaired first, otherwise almost every input would be
// rejected by the checksum and the field validation would never run.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "container/crc32.hpp"
#include "container/endian.hpp"
#include "container/header.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    bardzip::HeaderBytes bytes{};
    std::memcpy(bytes.data(), data, size < bytes.size() ? size : bytes.size());

    // Half of the inputs keep the checksum as given (to cover the rejection path).
    if (size == 0 || (data[0] & 1) != 0) {
        const std::array<std::uint8_t, 4> magic{ 0x42, 0x5A, 0x50, 0x31 };
        std::memcpy(bytes.data(), magic.data(), magic.size());

        const auto checksum = bardzip::crc32(
            std::span<const std::uint8_t>{ bytes }.first(32));
        const auto encoded = bardzip::encode_u32_le(checksum);
        std::memcpy(bytes.data() + 32, encoded.data(), encoded.size());
    }

    try {
        const auto header = bardzip::parse_header(bytes);

        // A parsed header must survive a serialize/parse round trip.
        const auto again = bardzip::parse_header(bardzip::serialize_header(header));

        if (again.original_size != header.original_size ||
            again.payload_size != header.payload_size ||
            again.original_crc32 != header.original_crc32 ||
            again.payload_crc32 != header.payload_crc32 ||
            again.compression != header.compression) {
            std::abort();
        }
    }
    catch (const std::exception&) {
    }

    return 0;
}
