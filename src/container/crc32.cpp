#include "container/crc32.hpp"

#include <cstddef>

namespace bardzip {

void Crc32::update(std::span<const std::uint8_t> bytes) noexcept {
    auto crc = state_;

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

    state_ = crc;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    Crc32 crc;
    crc.update(bytes);
    return crc.value();
}

}
