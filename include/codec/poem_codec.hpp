#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bardzip {

inline constexpr std::string_view poem_marker = "BARDZIP-POEM 1 DICT 1";
inline constexpr std::string_view stanza_refrain = "четверг не наступил.";

using Stanza = std::array<std::string, 3>;

Stanza encode_stanza(std::uint8_t first, std::uint8_t second);
std::array<std::uint8_t, 2> decode_stanza(const Stanza& stanza);

std::string encode_poem(std::span<const std::uint8_t> container);

std::vector<std::uint8_t> decode_poem(std::string_view poem);

}
