#include "container/header.hpp"

#include <algorithm>
#include <stdexcept>

#include "container/crc32.hpp"
#include "container/endian.hpp"

namespace bardzip {

namespace {

constexpr std::size_t version_offset = 4;
constexpr std::size_t compression_offset = 5;
constexpr std::size_t reserved_offset = 6;
constexpr std::size_t original_size_offset = 8;
constexpr std::size_t payload_size_offset = 16;
constexpr std::size_t original_crc_offset = 24;
constexpr std::size_t payload_crc_offset = 28;
constexpr std::size_t header_crc_offset = 32;

constexpr std::array<std::uint8_t, 4> magic{ 0x42, 0x5A, 0x50, 0x31 };  // "BZP1"

template <std::size_t N>
std::array<std::uint8_t, N> read_field(
    std::span<const std::uint8_t> bytes,
    std::size_t offset
) {
    std::array<std::uint8_t, N> field{};
    std::copy_n(bytes.begin() + offset, N, field.begin());
    return field;
}

template <std::size_t N>
void write_field(
    HeaderBytes& bytes,
    std::size_t offset,
    const std::array<std::uint8_t, N>& field
) {
    std::copy(field.begin(), field.end(), bytes.begin() + offset);
}

void validate_fields(const Header& header) {
    switch (header.compression) {
    case CompressionMethod::None:
        if (header.original_size != header.payload_size) {
            throw std::invalid_argument(
                "Uncompressed payload size must equal original size"
            );
        }

        if (header.original_crc32 != header.payload_crc32) {
            throw std::invalid_argument(
                "Uncompressed payload CRC must equal original CRC"
            );
        }
        break;

    case CompressionMethod::Zstd:
        if (header.payload_size == 0) {
            throw std::invalid_argument("Compressed payload must not be empty");
        }
        break;
    }
}

}  // namespace

HeaderBytes serialize_header(const Header& header) {
    if (header.version != 1 ||
        !compression_method_from_byte(static_cast<std::uint8_t>(header.compression)))
    {
        throw std::invalid_argument("Unsupported header settings");
    }

    validate_fields(header);

    HeaderBytes bytes{};

    write_field(bytes, 0, magic);
    bytes[version_offset] = header.version;
    bytes[compression_offset] = static_cast<std::uint8_t>(header.compression);

    write_field(bytes, original_size_offset, encode_u64_le(header.original_size));
    write_field(bytes, payload_size_offset, encode_u64_le(header.payload_size));
    write_field(bytes, original_crc_offset, encode_u32_le(header.original_crc32));
    write_field(bytes, payload_crc_offset, encode_u32_le(header.payload_crc32));

    const auto checksum = crc32(
        std::span<const std::uint8_t>{bytes}.first(header_crc_offset)
    );

    write_field(bytes, header_crc_offset, encode_u32_le(checksum));

    return bytes;
}

Header parse_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != header_size) {
        throw std::invalid_argument("Invalid header size");
    }

    if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
        throw std::invalid_argument("Invalid container magic");
    }

    const auto stored_checksum =
        decode_u32_le(read_field<4>(bytes, header_crc_offset));
    const auto actual_checksum = crc32(bytes.first(header_crc_offset));

    if (stored_checksum != actual_checksum) {
        throw std::invalid_argument("Invalid header checksum");
    }

    if (bytes[version_offset] != 1) {
        throw std::invalid_argument("Unsupported container version");
    }

    const auto method = compression_method_from_byte(bytes[compression_offset]);

    if (!method) {
        throw std::invalid_argument("Unsupported compression method");
    }

    if (bytes[reserved_offset] != 0 || bytes[reserved_offset + 1] != 0) {
        throw std::invalid_argument("Reserved bytes must be zero");
    }

    Header header;

    header.version = bytes[version_offset];
    header.compression = *method;
    header.original_size =
        decode_u64_le(read_field<8>(bytes, original_size_offset));
    header.payload_size =
        decode_u64_le(read_field<8>(bytes, payload_size_offset));
    header.original_crc32 =
        decode_u32_le(read_field<4>(bytes, original_crc_offset));
    header.payload_crc32 =
        decode_u32_le(read_field<4>(bytes, payload_crc_offset));

    validate_fields(header);

    return header;
}

}
