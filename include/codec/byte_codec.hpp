#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "codec/dictionary.hpp"

namespace bardzip {

std::string encode_byte(
    std::uint8_t value,
    const Dictionary& high,
    const Dictionary& low
);

std::uint8_t decode_byte(
    std::string_view line,
    const Dictionary& high,
    const Dictionary& low
);

}
