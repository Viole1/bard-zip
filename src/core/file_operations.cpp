#include "core/file_operations.hpp"

#include <iomanip>
#include <iostream>

#include "codec/poem_codec.hpp"
#include "container/container.hpp"
#include "container/header.hpp"
#include "io/file_reader.hpp"
#include "io/file_writer.hpp"

namespace bardzip {

EncodeReport encode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    CompressionMode mode
) {
    const auto original = read_binary_file(input_path);
    const auto container = build_container(original, mode);
    const auto poem = encode_poem(container);

    write_text_file(output_path, poem);

    const auto header = parse_header(
        std::span<const std::uint8_t>{container}.first(header_size)
    );

    return {
        header.compression,
        header.original_size,
        header.payload_size,
        container.size(),
        poem.size(),
        container.size() / 2 + container.size() % 2
    };
}

void decode_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    std::uint64_t max_output
) {
    const auto poem = read_text_file(input_path);
    const auto container = decode_poem(poem);
    const auto original = extract_container(container, max_output);

    write_binary_file(output_path, original);
}

void inspect_file(const std::filesystem::path& input_path) {
    const auto poem = read_text_file(input_path);
    const auto container = decode_poem(poem);
    const auto header = parse_header(
        std::span<const std::uint8_t>{container}.first(header_size)
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

    std::cout << "Stanzas:        "
        << container.size() / 2 + container.size() % 2 << '\n';

    std::cout << "Payload integrity: not checked\n";
}

void verify_file(
    const std::filesystem::path& input_path,
    std::uint64_t max_output
) {
    const auto poem = read_text_file(input_path);
    const auto container = decode_poem(poem);

    static_cast<void>(extract_container(container, max_output));

    std::cout << "Verification successful.\n";
}

}
