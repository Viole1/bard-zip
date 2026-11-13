#pragma once

#include <cstdint>
#include <span>

namespace bardzip {

// CRC-32/ISO-HDLC (poly 0xEDB88320 reflected, init/xorout 0xFFFFFFFF).
std::uint32_t crc32(std::span<const std::uint8_t> bytes);

}
