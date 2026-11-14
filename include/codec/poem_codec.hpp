#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "container/header.hpp"
#include "io/byte_sink.hpp"

namespace bardzip {

inline constexpr std::string_view poem_marker = "BARDZIP-POEM 1 DICT 1";
inline constexpr std::string_view stanza_refrain = "четверг не наступил.";

// A poem line longer than this is rejected while reading.
inline constexpr std::size_t max_poem_line_length = 4096;

using Stanza = std::array<std::string, 3>;

Stanza encode_stanza(std::uint8_t first, std::uint8_t second);
std::array<std::uint8_t, 2> decode_stanza(const Stanza& stanza);

using TextSink = std::function<void(std::string_view)>;

// Streaming poem writer. The marker is emitted immediately; every two bytes
// written produce one stanza, finish() pads an odd trailing byte with zero.
// Text reaches the sink in blocks of about 64 KiB.
class PoemEncoder {
public:
    explicit PoemEncoder(TextSink sink);

    void write(std::span<const std::uint8_t> bytes);
    void finish();

    std::size_t stanza_count() const noexcept { return stanza_count_; }
    std::size_t text_size() const noexcept { return text_size_; }

private:
    void add_stanza(std::uint8_t first, std::uint8_t second);
    void flush();

    TextSink sink_;
    std::string buffer_;
    std::size_t stanza_count_ = 0;
    std::size_t text_size_ = 0;
    bool has_pending_ = false;
    std::uint8_t pending_ = 0;
    bool finished_ = false;
};

std::string encode_poem(std::span<const std::uint8_t> container);

using HeaderCallback = std::function<void(const Header&)>;

// Reads a poem from the stream without loading it entirely into memory.
// Validates the poem structure, calls on_header (may be empty) as soon as the
// container header is known, then passes the payload bytes to on_payload in
// blocks. Verifies that the poem length and padding match the header.
Header decode_poem_stream(
    std::istream& poem,
    const HeaderCallback& on_header,
    const ByteSink& on_payload
);

std::vector<std::uint8_t> decode_poem(std::string_view poem);

}
