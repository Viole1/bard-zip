#include "codec/poem_codec.hpp"

#include <limits>
#include <sstream>
#include <stdexcept>

#include "codec/byte_codec.hpp"
#include "codec/dictionary.hpp"
#include "container/header.hpp"

namespace bardzip {

Stanza encode_stanza(std::uint8_t first, std::uint8_t second) {
    Stanza stanza;

    stanza[0] = encode_byte(first, adjectives, nouns);
    stanza[1] = encode_byte(second, verbs, objects) + ".";
    stanza[2] = std::string(stanza_refrain);

    return stanza;
}

std::array<std::uint8_t, 2> decode_stanza(const Stanza& stanza) {
    if (stanza[2] != stanza_refrain) {
        throw std::invalid_argument("Invalid stanza format.");
    }

    std::string_view second_line = stanza[1];

    if (!second_line.ends_with('.')) {
        throw std::invalid_argument(
            "Invalid second line: expected a final period"
        );
    }

    second_line.remove_suffix(1);

    return {
        decode_byte(stanza[0], adjectives, nouns),
        decode_byte(second_line, verbs, objects)
    };
}

std::string encode_poem(std::span<const std::uint8_t> container) {
    std::string poem(poem_marker);
    poem += "\n\n";

    for (std::size_t i = 0; i < container.size();) {
        const auto first = container[i++];

        const auto second = i < container.size()
            ? container[i++]
            : std::uint8_t{ 0 };

        const auto stanza = encode_stanza(first, second);

        poem += stanza[0] + "\n";
        poem += stanza[1] + "\n";
        poem += stanza[2] + "\n\n";
    }

    return poem;
}

std::vector<std::uint8_t> decode_poem(std::string_view poem) {
    std::istringstream stream{ std::string(poem) };

    std::string line;
    std::string header_line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!line.empty()) {
            header_line = std::move(line);
            break;
        }
    }

    if (header_line != poem_marker) {
        throw std::invalid_argument("Invalid poem header");
    }

    std::vector<std::string> lines;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }

    if (!stream.eof()) {
        throw std::runtime_error("Failed to read poem text");
    }

    if (lines.size() % 3 != 0) {
        throw std::invalid_argument(
            "Invalid poem format: incomplete stanza"
        );
    }

    const auto stanza_count = lines.size() / 3;

    if (stanza_count > (std::numeric_limits<std::size_t>::max)() / 2) {
        throw std::overflow_error("Decoded poem is too large");
    }

    std::vector<std::uint8_t> decoded;
    decoded.reserve(stanza_count * 2);

    for (std::size_t i = 0; i < lines.size(); i += 3) {
        const Stanza stanza{ lines[i], lines[i + 1], lines[i + 2] };

        try {
            const auto stanza_bytes = decode_stanza(stanza);

            decoded.push_back(stanza_bytes[0]);
            decoded.push_back(stanza_bytes[1]);
        }
        catch (const std::exception& error) {
            const auto stanza_number = i / 3 + 1;

            throw std::invalid_argument(
                "Failed to decode stanza "
                + std::to_string(stanza_number)
                + ": "
                + error.what()
            );
        }
    }

    if (decoded.size() < header_size) {
        throw std::invalid_argument(
            "Decoded poem is too small to contain a valid container header"
        );
    }

    const auto header = parse_header(
        std::span<const std::uint8_t>{decoded}.first(header_size)
    );

    if constexpr (
        std::numeric_limits<std::size_t>::max() <
        std::numeric_limits<std::uint64_t>::max())
    {
        if (header.payload_size > std::numeric_limits<std::size_t>::max()) {
            throw std::overflow_error(
                "Payload size cannot be represented by size_t"
            );
        }
    }

    const auto payload_size = static_cast<std::size_t>(header.payload_size);

    if (payload_size > (std::numeric_limits<std::size_t>::max)() - header_size) {
        throw std::overflow_error("Container size overflow");
    }

    const auto container_size = header_size + payload_size;

    const auto expected_stanza_count =
        container_size / 2 + container_size % 2;

    if (decoded.size() / 2 != expected_stanza_count) {
        throw std::invalid_argument(
            "Poem size does not match container payload size"
        );
    }

    if (container_size % 2 != 0) {
        if (decoded.size() != container_size + 1) {
            throw std::invalid_argument("Invalid encoded container size");
        }

        if (decoded.back() != 0) {
            throw std::invalid_argument(
                "Invalid poem padding: expected zero byte"
            );
        }

        decoded.pop_back();
    }
    else if (decoded.size() != container_size) {
        throw std::invalid_argument("Unexpected poem padding");
    }

    return decoded;
}

}
