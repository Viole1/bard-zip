#include "codec/poem_codec.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

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

namespace {

// Every valid first line and second line (with its final period) maps to
// exactly one byte; 256 entries each. The line breaks are already removed by
// the line reader, so keys contain none.
struct DecodingTables {
    std::unordered_map<std::string, std::uint8_t> first_lines;
    std::unordered_map<std::string, std::uint8_t> second_lines;
};

const DecodingTables& decoding_tables() {
    static const DecodingTables tables = [] {
        DecodingTables result;

        result.first_lines.reserve(256);
        result.second_lines.reserve(256);

        for (std::size_t value = 0; value < 256; ++value) {
            const auto byte = static_cast<std::uint8_t>(value);

            const auto first = result.first_lines.emplace(
                encode_byte(byte, adjectives, nouns), byte);
            const auto second = result.second_lines.emplace(
                encode_byte(byte, verbs, objects) + ".", byte);

            // A duplicate line would make decoding ambiguous.
            if (!first.second || !second.second) {
                throw std::logic_error("Dictionaries produce duplicate poem lines");
            }
        }

        return result;
    }();

    return tables;
}

}

std::array<std::uint8_t, 2> decode_stanza(const Stanza& stanza) {
    if (stanza[2] != stanza_refrain) {
        throw std::invalid_argument("Invalid stanza format.");
    }

    const auto& tables = decoding_tables();

    const auto first = tables.first_lines.find(stanza[0]);

    if (first == tables.first_lines.end()) {
        throw std::invalid_argument("Unknown first line in the stanza");
    }

    const auto second = tables.second_lines.find(stanza[1]);

    if (second == tables.second_lines.end()) {
        if (!std::string_view(stanza[1]).ends_with('.')) {
            throw std::invalid_argument(
                "Invalid second line: expected a final period"
            );
        }

        throw std::invalid_argument("Unknown second line in the stanza");
    }

    return { first->second, second->second };
}

PoemEncoder::PoemEncoder(TextSink sink)
    : sink_(std::move(sink))
{
    buffer_.reserve(stream_block_size + 256);
    buffer_ += poem_marker;
    buffer_ += "\n\n";
}

void PoemEncoder::write(std::span<const std::uint8_t> bytes) {
    if (finished_) {
        throw std::logic_error("Cannot write to a finished poem encoder");
    }

    for (const auto byte : bytes) {
        if (has_pending_) {
            add_stanza(pending_, byte);
            has_pending_ = false;
        }
        else {
            pending_ = byte;
            has_pending_ = true;
        }
    }
}

void PoemEncoder::finish() {
    if (finished_) {
        throw std::logic_error("Poem encoder is already finished");
    }

    if (has_pending_) {
        add_stanza(pending_, 0);
        has_pending_ = false;
    }

    finished_ = true;
    flush();
}

namespace {

// Every first line and every second line has only 256 possible values, so they
// are built once (with their line break) instead of once per stanza.
struct EncodingTables {
    std::array<std::string, 256> first_lines;
    std::array<std::string, 256> second_lines;
};

const EncodingTables& encoding_tables() {
    static const EncodingTables tables = [] {
        EncodingTables result;

        for (std::size_t value = 0; value < 256; ++value) {
            const auto byte = static_cast<std::uint8_t>(value);

            result.first_lines[value] = encode_byte(byte, adjectives, nouns) + "\n";
            result.second_lines[value] = encode_byte(byte, verbs, objects) + ".\n";
        }

        return result;
    }();

    return tables;
}

}

void PoemEncoder::add_stanza(std::uint8_t first, std::uint8_t second) {
    const auto& tables = encoding_tables();

    buffer_ += tables.first_lines[first];
    buffer_ += tables.second_lines[second];
    buffer_ += stanza_refrain;
    buffer_ += "\n\n";

    ++stanza_count_;

    if (buffer_.size() >= stream_block_size) {
        flush();
    }
}

void PoemEncoder::flush() {
    if (buffer_.empty()) {
        return;
    }

    text_size_ += buffer_.size();
    sink_(buffer_);
    buffer_.clear();
}

std::string encode_poem(std::span<const std::uint8_t> container) {
    std::string poem;

    PoemEncoder encoder([&](std::string_view text) { poem += text; });

    encoder.write(container);
    encoder.finish();

    return poem;
}

namespace {

// Reads non-empty lines from a stream in blocks, so memory use does not depend
// on the poem size and a single line can never grow without bound.
class LineReader {
public:
    explicit LineReader(std::istream& input)
        : input_(input), block_(stream_block_size)
    {
    }

    bool next_line(std::string& line) {
        while (true) {
            line.clear();
            bool terminated = false;

            while (position_ != length_ || fill()) {
                const char* begin = block_.data() + position_;
                const char* end = block_.data() + length_;
                const char* newline = std::find(begin, end, '\n');

                line.append(begin, newline);

                // +1 leaves room for the '\r' of a CRLF line ending.
                if (line.size() > max_poem_line_length + 1) {
                    throw std::invalid_argument("Poem line is too long");
                }

                if (newline != end) {
                    position_ += static_cast<std::size_t>(newline - begin) + 1;
                    terminated = true;
                    break;
                }

                position_ = length_;
            }

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (!line.empty()) {
                return true;
            }

            if (!terminated) {
                return false;
            }
        }
    }

private:
    bool fill() {
        input_.read(block_.data(), static_cast<std::streamsize>(block_.size()));

        if (input_.bad() || (input_.fail() && !input_.eof())) {
            throw std::runtime_error("Failed to read poem text");
        }

        length_ = static_cast<std::size_t>(input_.gcount());
        position_ = 0;

        return length_ != 0;
    }

    std::istream& input_;
    std::vector<char> block_;
    std::size_t position_ = 0;
    std::size_t length_ = 0;
};

class StanzaReader {
public:
    explicit StanzaReader(std::istream& input) : lines_(input) {}

    bool read_marker() {
        std::string line;
        return lines_.next_line(line) && line == poem_marker;
    }

    // Returns false at the end of the poem. The three lines are read into
    // members so their memory is reused for every stanza.
    bool next(std::array<std::uint8_t, 2>& bytes) {
        if (!lines_.next_line(stanza_[0])) {
            return false;
        }

        if (!lines_.next_line(stanza_[1]) || !lines_.next_line(stanza_[2])) {
            throw std::invalid_argument(
                "Invalid poem format: incomplete stanza"
            );
        }

        ++number_;

        try {
            bytes = decode_stanza(stanza_);
        }
        catch (const std::exception& error) {
            throw std::invalid_argument(
                "Failed to decode stanza "
                + std::to_string(number_)
                + ": "
                + error.what()
            );
        }

        return true;
    }

    bool has_more_lines() {
        std::string line;
        return lines_.next_line(line);
    }

private:
    LineReader lines_;
    Stanza stanza_;
    std::size_t number_ = 0;
};

constexpr const char* size_mismatch_message =
    "Poem size does not match container payload size";

}

Header decode_poem_stream(
    std::istream& poem,
    const HeaderCallback& on_header,
    const ByteSink& on_payload
) {
    StanzaReader reader(poem);

    if (!reader.read_marker()) {
        throw std::invalid_argument("Invalid poem header");
    }

    std::array<std::uint8_t, 2> pair{};
    HeaderBytes header_bytes{};

    for (std::size_t i = 0; i < header_size; i += 2) {
        if (!reader.next(pair)) {
            throw std::invalid_argument(
                "Decoded poem is too small to contain a valid container header"
            );
        }

        header_bytes[i] = pair[0];
        header_bytes[i + 1] = pair[1];
    }

    const auto header = parse_header(header_bytes);

    if (header.payload_size >
        (std::numeric_limits<std::uint64_t>::max)() - header_size)
    {
        throw std::overflow_error("Container size overflow");
    }

    if (on_header) {
        on_header(header);
    }

    std::vector<std::uint8_t> block;
    block.reserve(stream_block_size + 2);

    const auto flush = [&] {
        if (!block.empty()) {
            on_payload(block);
            block.clear();
        }
    };

    for (std::uint64_t remaining = header.payload_size; remaining != 0;) {
        if (!reader.next(pair)) {
            throw std::invalid_argument(size_mismatch_message);
        }

        block.push_back(pair[0]);

        if (remaining == 1) {
            if (pair[1] != 0) {
                throw std::invalid_argument(
                    "Invalid poem padding: expected zero byte"
                );
            }

            remaining = 0;
        }
        else {
            block.push_back(pair[1]);
            remaining -= 2;
        }

        if (block.size() >= stream_block_size) {
            flush();
        }
    }

    flush();

    if (reader.has_more_lines()) {
        throw std::invalid_argument(size_mismatch_message);
    }

    return header;
}

std::vector<std::uint8_t> decode_poem(std::string_view poem) {
    std::istringstream stream{ std::string(poem) };

    std::vector<std::uint8_t> payload;

    const auto header = decode_poem_stream(
        stream,
        {},
        [&](std::span<const std::uint8_t> chunk) {
            payload.insert(payload.end(), chunk.begin(), chunk.end());
        }
    );

    // The header was fully validated, so serializing it restores the original bytes.
    const auto header_bytes = serialize_header(header);

    std::vector<std::uint8_t> container(header_bytes.begin(), header_bytes.end());
    container.insert(container.end(), payload.begin(), payload.end());

    return container;
}

}
