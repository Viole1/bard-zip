#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace bardzip {

// Size of the working blocks used by every streaming stage (64 KiB).
inline constexpr std::size_t stream_block_size = 64 * 1024;

// Receives the next chunk of a byte stream. The chunk is only valid during the call.
using ByteSink = std::function<void(std::span<const std::uint8_t>)>;

}
