#include "container/crc32.hpp"

#include <cstddef>

namespace bardzip {

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFFu;

    for (const auto byte : bytes) {
        crc ^= byte;

        for (std::size_t i = 0; i < 8; ++i) {
            if ((crc & 1) != 0) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            }
            else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

}
