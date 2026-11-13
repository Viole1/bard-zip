#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "codec/byte_codec.hpp"
#include "codec/dictionary.hpp"
#include "codec/poem_codec.hpp"
#include "container/container.hpp"
#include "test_support.hpp"

using namespace bardzip;

TEST(dictionaries_have_unique_words) {
    for (const auto* dictionary : { &adjectives, &nouns, &verbs, &objects }) {
        const std::set<std::string_view> unique(dictionary->begin(), dictionary->end());
        CHECK(unique.size() == dictionary->size());
    }
}

TEST(byte_codec_round_trip_all_values) {
    std::set<std::string> lines;

    for (int value = 0; value < 256; ++value) {
        const auto byte = static_cast<std::uint8_t>(value);
        const auto line = encode_byte(byte, adjectives, nouns);

        CHECK(decode_byte(line, adjectives, nouns) == byte);
        lines.insert(line);
    }

    CHECK(lines.size() == 256);
}

TEST(byte_codec_rejects_invalid_lines) {
    CHECK_THROWS(decode_byte("угрюмый", adjectives, nouns));
    CHECK_THROWS(decode_byte("угрюмый огурцы", adjectives, nouns));
    CHECK_THROWS(decode_byte("неизвестный баклажан", adjectives, nouns));
}

TEST(stanza_round_trip_all_pairs) {
    for (int first = 0; first < 256; ++first) {
        for (int second = 0; second < 256; ++second) {
            const std::array<std::uint8_t, 2> original{
                static_cast<std::uint8_t>(first),
                static_cast<std::uint8_t>(second)
            };

            CHECK(decode_stanza(encode_stanza(original[0], original[1])) == original);
        }
    }
}

TEST(stanza_rejects_wrong_refrain_and_period) {
    auto stanza = encode_stanza(0x12, 0x34);

    auto wrong_refrain = stanza;
    wrong_refrain[2] = "пятница наступила.";
    CHECK_THROWS(decode_stanza(wrong_refrain));

    auto no_period = stanza;
    no_period[1].pop_back();
    CHECK_THROWS(decode_stanza(no_period));
}

TEST(poem_round_trip_odd_and_even_containers) {
    for (std::size_t size = 0; size < 6; ++size) {
        const std::vector<std::uint8_t> original(size, 0xAB);
        const auto container = build_container(original);
        const auto poem = encode_poem(container);

        CHECK(decode_poem(poem) == container);
    }
}

TEST(poem_accepts_crlf) {
    const std::vector<std::uint8_t> original{ 1, 2, 3 };
    const auto container = build_container(original);
    auto poem = encode_poem(container);

    std::string crlf;
    for (const char ch : poem) {
        if (ch == '\n') {
            crlf += '\r';
        }
        crlf += ch;
    }

    CHECK(decode_poem(crlf) == container);
}

TEST(poem_rejects_structural_damage) {
    const auto container = build_container(std::vector<std::uint8_t>{ 1, 2, 3 });
    const auto poem = encode_poem(container);

    CHECK_THROWS(decode_poem(poem.substr(poem.find('\n'))));

    std::string truncated = poem;
    truncated.erase(truncated.size() - 2 - std::string(stanza_refrain).size());
    CHECK_THROWS(decode_poem(truncated));

    CHECK_THROWS(decode_poem(poem + "угрюмый баклажан\nсозерцает протокол.\n"
        + std::string(stanza_refrain) + "\n"));
}
