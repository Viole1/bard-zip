// libFuzzer target: the whole poem reader (line splitting, stanza decoding,
// header parsing, padding and length checks). Any exception derived from
// std::exception is a valid "rejected" outcome; crashes, hangs and memory
// errors are bugs.

#include <cstddef>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>

#include "codec/poem_codec.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        std::istringstream stream(
            std::string(reinterpret_cast<const char*>(data), size));

        bardzip::decode_poem_stream(
            stream, {}, [](std::span<const std::uint8_t>) {});
    }
    catch (const std::exception&) {
    }

    return 0;
}
