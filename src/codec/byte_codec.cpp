#include "codec/byte_codec.hpp"

#include <algorithm>
#include <stdexcept>

namespace bardzip {

// Ex: 0x2B = 0010 1011 -> high_index = 2, low_index = 11
std::string encode_byte(
    std::uint8_t value,
    const Dictionary& high,
    const Dictionary& low
) {
    const auto high_index = value >> 4;
    const auto low_index = value & 0x0F;

    return std::string(high[high_index]) + " " + std::string(low[low_index]);
}

std::uint8_t decode_byte(
    std::string_view line,
    const Dictionary& high,
    const Dictionary& low
) {
    const auto pos = line.find(' ');

    if (pos == std::string_view::npos) {
        throw std::invalid_argument(
            "Invalid input format: Expected two words separated by a space"
        );
    }

    const auto first = line.substr(0, pos);
    const auto second = line.substr(pos + 1);

    const auto first_it = std::find(high.begin(), high.end(), first);
    const auto second_it = std::find(low.begin(), low.end(), second);

    if (first_it == high.end()) {
        throw std::invalid_argument("Unknown word in the first dictionary");
    }

    if (second_it == low.end()) {
        throw std::invalid_argument("Unknown word in the second dictionary");
    }

    const auto high_index = first_it - high.begin();
    const auto low_index = second_it - low.begin();

    return static_cast<std::uint8_t>((high_index << 4) | low_index);
}

}
