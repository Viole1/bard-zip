#pragma once

#include <cstdint>
#include <span>

namespace bardzip {

// CRC-32/ISO-HDLC (poly 0xEDB88320 reflected, init/xorout 0xFFFFFFFF).

class Crc32 {
public:
    void update(std::span<const std::uint8_t> bytes) noexcept;

    std::uint32_t value() const noexcept { return ~state_; }

private:
    std::uint32_t state_ = 0xFFFFFFFFu;
};

std::uint32_t crc32(std::span<const std::uint8_t> bytes);

}
