#include <cstdint>
#include <exception>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "codec/poem_codec.hpp"
#include "compression/compression.hpp"
#include "container/container.hpp"
#include "container/header.hpp"
#include "test_support.hpp"

using namespace bardzip;

// A small deterministic mutation fuzzer that runs with the normal tests. It is
// not a replacement for libFuzzer (tools/fuzz), but it catches regressions in
// CI: damaged input must be rejected with a std::exception, never crash, hang
// or allocate absurd amounts of memory.

namespace {

class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_;
    }

    std::size_t below(std::size_t limit) { return limit == 0 ? 0 : next() % limit; }

private:
    std::uint64_t state_;
};

std::string mutate(std::string text, Random& random) {
    const auto rounds = 1 + random.below(3);

    for (std::size_t round = 0; round < rounds; ++round) {
        if (text.empty()) {
            text.push_back(static_cast<char>(random.next()));
            continue;
        }

        const auto position = random.below(text.size());

        switch (random.below(7)) {
        case 0:  // flip one bit
            text[position] = static_cast<char>(text[position] ^ (1 << random.below(8)));
            break;
        case 1:  // replace a byte
            text[position] = static_cast<char>(random.next());
            break;
        case 2:  // delete a range
            text.erase(position, 1 + random.below(40));
            break;
        case 3:  // duplicate a range
            text.insert(position, text.substr(position, 1 + random.below(60)));
            break;
        case 4:  // truncate
            text.resize(position);
            break;
        case 5:  // insert random bytes
            text.insert(position, std::string(1 + random.below(5), static_cast<char>(random.next())));
            break;
        default:  // break or add a line
            text.insert(position, "\n");
            break;
        }
    }

    return text;
}

// Runs the whole in-memory decode path; returns true if it accepted the input.
bool decode_accepts(const std::string& poem) {
    try {
        const auto container = decode_poem(poem);
        static_cast<void>(extract_container(container, 1 << 20));
        return true;
    }
    catch (const std::bad_alloc&) {
        throw std::runtime_error("allocation failure on damaged input");
    }
    catch (const std::exception&) {
        return false;
    }
}

}

TEST(mutated_poems_are_rejected_or_decoded_without_crashing) {
    std::vector<std::string> seeds;

    for (const auto mode : { CompressionMode::None, CompressionMode::Zstd }) {
        for (const std::size_t size : { 0u, 1u, 5u, 40u }) {
            const std::vector<std::uint8_t> original(size, 'a');
            seeds.push_back(encode_poem(build_container(original, mode)));
        }
    }

    Random random(0xBADC0FFEE);
    std::size_t accepted = 0;
    constexpr std::size_t iterations = 20000;

    for (std::size_t i = 0; i < iterations; ++i) {
        const auto& seed = seeds[random.below(seeds.size())];

        if (decode_accepts(mutate(seed, random))) {
            ++accepted;
        }
    }

    // Almost every mutation must be rejected; a few harmless ones (extra blank
    // lines, CRLF) legitimately decode.
    CHECK(accepted < iterations / 2);
}

TEST(random_bytes_never_break_the_zstd_decompressor) {
    Random random(42);

    for (std::size_t i = 0; i < 5000; ++i) {
        std::vector<std::uint8_t> garbage(random.below(200));
        for (auto& byte : garbage) {
            byte = static_cast<std::uint8_t>(random.next());
        }

        // Half of the inputs start like a real frame so deeper code runs.
        if (garbage.size() >= 4 && (i & 1) != 0) {
            garbage[0] = 0x28;
            garbage[1] = 0xB5;
            garbage[2] = 0x2F;
            garbage[3] = 0xFD;
        }

        const auto expected = random.below(5000);

        try {
            const auto result = decompress(garbage, CompressionMethod::Zstd, expected);
            CHECK(result.size() == expected);
        }
        catch (const std::bad_alloc&) {
            throw std::runtime_error("allocation failure on garbage input");
        }
        catch (const std::exception&) {
        }
    }
}

TEST(mutated_headers_are_rejected_or_round_trip) {
    Header header;
    header.original_size = 5;
    header.payload_size = 5;
    header.original_crc32 = 0x1234;
    header.payload_crc32 = 0x1234;

    const auto valid = serialize_header(header);
    Random random(7);

    for (std::size_t i = 0; i < 20000; ++i) {
        auto bytes = valid;
        const auto flips = 1 + random.below(3);

        for (std::size_t f = 0; f < flips; ++f) {
            bytes[random.below(bytes.size())] ^= static_cast<std::uint8_t>(1 + random.below(255));
        }

        try {
            const auto parsed = parse_header(bytes);
            // Whatever is accepted must serialize and parse again identically.
            const auto again = parse_header(serialize_header(parsed));
            CHECK(again.original_size == parsed.original_size);
            CHECK(again.payload_size == parsed.payload_size);
            CHECK(again.compression == parsed.compression);
        }
        catch (const std::exception&) {
        }
    }
}
