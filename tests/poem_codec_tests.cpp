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

namespace {

// The original decoder (word-by-word search in the 16-word dictionaries),
// kept as the reference for the table-based decode_stanza.
std::array<std::uint8_t, 2> reference_decode_stanza(const Stanza& stanza) {
    if (stanza[2] != stanza_refrain) {
        throw std::invalid_argument("Invalid stanza format.");
    }

    std::string_view second_line = stanza[1];

    if (!second_line.ends_with('.')) {
        throw std::invalid_argument("Invalid second line: expected a final period");
    }

    second_line.remove_suffix(1);

    return {
        decode_byte(stanza[0], adjectives, nouns),
        decode_byte(second_line, verbs, objects)
    };
}

// Independent encoder reference: builds the poem text through encode_stanza,
// which does not use the precomputed tables of PoemEncoder.
std::string reference_poem(std::span<const std::uint8_t> bytes) {
    std::string poem(poem_marker);
    poem += "\n\n";

    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        const auto second = i + 1 < bytes.size() ? bytes[i + 1] : std::uint8_t{ 0 };
        const auto stanza = encode_stanza(bytes[i], second);
        poem += stanza[0] + "\n" + stanza[1] + "\n" + stanza[2] + "\n\n";
    }

    return poem;
}

}

TEST(table_decoder_matches_reference_for_all_pairs) {
    for (int first = 0; first < 256; ++first) {
        for (int second = 0; second < 256; ++second) {
            const auto stanza = encode_stanza(
                static_cast<std::uint8_t>(first), static_cast<std::uint8_t>(second));

            CHECK(decode_stanza(stanza) == reference_decode_stanza(stanza));
            CHECK(decode_stanza(stanza)[0] == first);
            CHECK(decode_stanza(stanza)[1] == second);
        }
    }
}

TEST(table_decoder_rejects_what_the_reference_rejects) {
    const auto good = encode_stanza(0x2B, 0x13);
    const std::string refrain(stanza_refrain);

    std::vector<Stanza> bad;
    const auto variant = [&](std::size_t line, std::string text) {
        auto stanza = good;
        stanza[line] = std::move(text);
        bad.push_back(stanza);
    };

    variant(0, "");                                 // empty first line
    variant(0, "угрюмый");                          // one word only
    variant(0, "неизвестный баклажан");             // unknown word
    variant(0, "угрюмый  баклажан");                // double space
    variant(0, " угрюмый баклажан");                // leading space
    variant(0, "угрюмый баклажан ");                // trailing space
    variant(0, "баклажан угрюмый");                 // swapped order
    variant(0, "созерцает бренность");              // words from the other dictionaries
    variant(0, "Угрюмый баклажан");                 // case matters
    variant(1, "");
    variant(1, ".");
    variant(1, "созерцает бренность");              // no final period
    variant(1, "созерцает бренность..");            // two periods
    variant(1, "угрюмый баклажан.");                // first-line words in the second line
    variant(1, "созерцает  бренность.");
    variant(1, "созерцает бренность. ");
    variant(2, "");
    variant(2, refrain.substr(0, refrain.size() - 1));
    variant(2, refrain + " ");
    variant(2, "пятница наступила.");

    for (const auto& stanza : bad) {
        CHECK_THROWS(reference_decode_stanza(stanza));
        CHECK_THROWS(decode_stanza(stanza));
    }

    CHECK_THROWS_WITH(decode_stanza(bad[11]), "final period");
}

TEST(poem_encoder_matches_independent_reference_for_all_pairs) {
    std::vector<std::uint8_t> data;
    data.reserve(65536 * 2 + 1);

    for (int first = 0; first < 256; ++first) {
        for (int second = 0; second < 256; ++second) {
            data.push_back(static_cast<std::uint8_t>(first));
            data.push_back(static_cast<std::uint8_t>(second));
        }
    }

    CHECK(encode_poem(data) == reference_poem(data));

    data.push_back(0x7F);  // odd length: padded with a zero byte
    CHECK(encode_poem(data) == reference_poem(data));
}

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
