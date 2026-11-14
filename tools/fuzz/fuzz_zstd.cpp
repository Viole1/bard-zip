// libFuzzer target: the streaming zstd decompressor. The first bytes choose the
// declared size and the chunk size the compressed data is fed in; the rest is
// the compressed data. The decoder must never produce more than the declared
// size and must never crash.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <span>

#include "compression/compression.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 4) {
        return 0;
    }

    const std::uint64_t expected = (static_cast<std::uint64_t>(data[0]) << 16)
        | (static_cast<std::uint64_t>(data[1]) << 8) | data[2];  // up to 16 MiB
    const std::size_t chunk = static_cast<std::size_t>(data[3]) + 1;
    const std::span<const std::uint8_t> payload{ data + 4, size - 4 };

    std::uint64_t produced = 0;

    try {
        bardzip::ZstdDecompressor decompressor(
            expected,
            [&](std::span<const std::uint8_t> out) {
                produced += out.size();

                if (produced > expected) {
                    std::abort();
                }
            });

        for (std::size_t offset = 0; offset < payload.size(); offset += chunk) {
            decompressor.write(payload.subspan(offset, (std::min)(chunk, payload.size() - offset)));
        }

        decompressor.finish();
    }
    catch (const std::exception&) {
    }

    return 0;
}
