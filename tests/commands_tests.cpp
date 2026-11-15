#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cli/arguments.hpp"
#include "codec/poem_codec.hpp"
#include "core/file_operations.hpp"
#include "core/size_format.hpp"
#include "detect/file_type.hpp"
#include "io/file_writer.hpp"
#include "test_support.hpp"

using namespace bardzip;
using namespace std::string_view_literals;

namespace {

std::vector<std::uint8_t> pseudo_random(std::size_t size, std::uint32_t seed = 99) {
    std::vector<std::uint8_t> data(size);
    for (auto& byte : data) {
        seed = seed * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(seed >> 24);
    }
    return data;
}

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return { text.begin(), text.end() };
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    write_binary_file(path, bytes);
}

std::string read_text(const std::filesystem::path& path) {
    const auto bytes = test::read_all(path);
    return { bytes.begin(), bytes.end() };
}

Arguments parse(std::vector<std::string> args) {
    args.insert(args.begin(), "bardzip");
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    return parse_arguments(static_cast<int>(argv.size()), argv.data());
}

// A file that starts with the given signature and continues with `tail`.
std::vector<std::uint8_t> with_signature(std::string_view signature, std::size_t tail) {
    auto data = bytes_of(signature);
    const auto rest = pseudo_random(tail);
    data.insert(data.end(), rest.begin(), rest.end());
    return data;
}

}

TEST(analysis_matches_the_real_encoder_exactly) {
    const auto dir = test::fresh_dir("analysis");

    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> inputs{
        { "empty", {} },
        { "one", { 0x42 } },
        { "seven", bytes_of("BardZip") },
        { "random", pseudo_random(5000) },
        { "repeated", std::vector<std::uint8_t>(60000, 'x') },
        { "text", bytes_of("Привет, мир! Thursday has not come.\r\n") },
    };

    for (const auto& [name, data] : inputs) {
        const auto input = dir / (name + ".bin");
        write_bytes(input, data);

        for (const auto mode : { CompressionMode::None, CompressionMode::Zstd, CompressionMode::Auto }) {
            const auto poem = dir / (name + ".poem");
            std::filesystem::remove(poem);
            const auto report = encode_file(input, poem, mode);
            const auto real = read_text(poem);

            for (const std::size_t stanzas : { 0u, 1u, 5u, 18u, 19u, 40u, 100000u }) {
                const auto analysis = analyze_file(input, mode, stanzas);

                CHECK(analysis.method == report.method);
                CHECK(analysis.original_size == report.original_size);
                CHECK(analysis.payload_size == report.payload_size);
                CHECK(analysis.container_size == report.container_size);
                CHECK(analysis.stanza_count == report.stanza_count);
                CHECK(analysis.poem_size == report.poem_size);
                CHECK(analysis.poem_size == real.size());

                // The head is exactly the beginning of the real poem.
                CHECK(analysis.head == real.substr(0, analysis.head.size()));
                CHECK(analysis.head.starts_with(std::string(poem_marker) + "\n\n"));

                if (stanzas >= analysis.stanza_count) {
                    CHECK(analysis.head == real);  // the whole poem fits
                }
            }
        }
    }
}

TEST(analysis_writes_nothing_and_has_no_size_limit) {
    const auto dir = test::fresh_dir("analysis_clean");
    const auto input = dir / "in.bin";
    write_bytes(input, pseudo_random(100000));

    const auto analysis = analyze_file(input, CompressionMode::Auto, 3);
    CHECK(analysis.original_size == 100000);
    CHECK(test::count_entries(dir) == 1);

    CHECK_THROWS(analyze_file(dir / "missing.bin", CompressionMode::Auto, 3));
}

TEST(poem_meter_matches_the_encoder_for_every_small_size) {
    for (std::size_t size = 0; size < 40; ++size) {
        const auto data = pseudo_random(size, static_cast<std::uint32_t>(size));

        PoemMeter meter;
        meter.write(std::span<const std::uint8_t>{ data }.first(size / 2));
        meter.write(std::span<const std::uint8_t>{ data }.subspan(size / 2));
        meter.finish();

        const auto poem = encode_poem(data);
        CHECK(poem_marker_text_size + meter.text_size() == poem.size());
        CHECK(meter.stanza_count() == size / 2 + size % 2);
    }
}

TEST(size_bounds_contain_real_poems_and_the_typical_size_is_close) {
    for (const std::size_t size : { 0u, 1u, 2u, 3u, 1000u, 100001u }) {
        const auto data = pseudo_random(size);
        const auto container = size + 36;
        const auto bounds = poem_size_bounds(container);
        const auto poem_size = encode_poem(data).size();

        // The poem of the data alone is smaller than that of the container (no header),
        // so compare the container poem instead.
        std::vector<std::uint8_t> container_bytes(container, 0);
        std::copy(data.begin(), data.end(), container_bytes.begin() + 36);
        const auto container_poem = encode_poem(container_bytes).size();

        CHECK(bounds.stanzas == container / 2 + container % 2);
        CHECK(bounds.smallest <= container_poem);
        CHECK(container_poem <= bounds.largest);
        CHECK(bounds.smallest <= bounds.typical && bounds.typical <= bounds.largest);
        static_cast<void>(poem_size);
    }

    // For random data the typical estimate is within 2 % of the real size.
    const auto data = pseudo_random(200000);
    std::vector<std::uint8_t> container(36, 0);
    container.insert(container.end(), data.begin(), data.end());
    const double real = static_cast<double>(encode_poem(container).size());
    const double typical = static_cast<double>(poem_size_bounds(container.size()).typical);
    CHECK(typical > real * 0.98 && typical < real * 1.02);
}

TEST(file_types_are_recognised_by_their_first_bytes) {
    using namespace std::string_view_literals;

    const auto type = [](std::string_view signature, std::size_t tail = 40) {
        return detect_file_type(with_signature(signature, tail));
    };

    CHECK(type("\x89PNG\r\n\x1A\n").extension == "png");
    CHECK(type("\xFF\xD8\xFF\xE0").extension == "jpg");
    CHECK(type("\xFF\xD8\xFF\xE1").description == "JPEG image");
    CHECK(type("GIF89a").extension == "gif");
    CHECK(type("GIF87a").extension == "gif");
    CHECK(type("%PDF-1.7").extension == "pdf");
    CHECK(type("PK\x03\x04").extension == "zip");
    CHECK(type("7z\xBC\xAF\x27\x1C").extension == "7z");
    CHECK(type("\x1F\x8B").extension == "gz");
    CHECK(type("Rar!\x1A\x07").extension == "rar");
    CHECK(type("SQLite format 3\x00"sv).extension == "sqlite");
    CHECK(type("MZ").extension == "exe");
    CHECK(type("\x00\x00\x00\x18" "ftypmp42"sv).extension == "mp4");
    CHECK(type("RIFF\x24\x00\x00\x00WAVE"sv).extension == "wav");
    CHECK(type("RIFF\x24\x00\x00\x00WEBP"sv).extension == "webp");
    CHECK(type("BM\x36\x00\x00\x00\x00\x00\x00\x00"sv).extension == "bmp");
    CHECK(type("ID3\x03").extension == "mp3");

    CHECK(detect_file_type({}).description == "Empty file");
}

TEST(plain_text_and_unknown_data_are_told_apart) {
    CHECK(detect_file_type(bytes_of("Hello, world!\r\nsecond line\n")).extension == "txt");
    CHECK(detect_file_type(bytes_of("Привет, мир! Это текст.")).description == "Plain text (UTF-8)");

    // A multi-byte character cut by the end of the sample is still text.
    auto cut = bytes_of("Привет");
    cut.pop_back();
    CHECK(detect_file_type(cut).extension == "txt");

    CHECK(detect_file_type(bytes_of("\xEF\xBB\xBFtext with a BOM")).description.find("byte order mark") != std::string_view::npos);

    CHECK(detect_file_type({ std::vector<std::uint8_t>{ 0x00, 0x01, 0x02, 0x03, 0xFE, 0xFD } }).description == "Unknown binary data");
    CHECK(detect_file_type(bytes_of("\xC3\x28 broken utf8")).description == "Unknown binary data");
    CHECK(detect_file_type(bytes_of("MM")).extension != "tif");  // too short to be a TIFF
}

TEST(identify_names_the_hidden_file_in_every_storage_mode) {
    const auto dir = test::fresh_dir("identify");

    struct Case {
        std::string name;
        std::vector<std::uint8_t> data;
        std::string extension;
    };

    std::vector<Case> cases{
        { "png", with_signature("\x89PNG\r\n\x1A\n"sv, 3000), "png" },
        { "jpg", with_signature("\xFF\xD8\xFF\xE0", 3000), "jpg" },
        { "zip", with_signature("PK\x03\x04", 3000), "zip" },
        { "pdf", with_signature("%PDF-1.4\n", 3000), "pdf" },
        { "txt", bytes_of("Just some text, nothing special.\n"), "txt" },
        { "bin", { 0, 1, 2, 3, 4, 5, 6, 7, 0xFE }, "" },
    };

    // A large, very compressible PNG forces the zstd path (a prefix of the frame is decoded).
    auto big = bytes_of("\x89PNG\r\n\x1A\n");
    big.resize(300000, 0);
    cases.push_back({ "bigpng", big, "png" });

    for (const auto& item : cases) {
        const auto input = dir / (item.name + ".bin");
        write_bytes(input, item.data);

        for (const auto mode : { CompressionMode::None, CompressionMode::Zstd, CompressionMode::Auto }) {
            const auto poem = dir / (item.name + ".poem");
            std::filesystem::remove(poem);
            encode_file(input, poem, mode, 0);

            const auto guess = identify_file(poem);
            CHECK(guess.extension == item.extension);
            CHECK(guess.original_size == item.data.size());
            CHECK(guess.first_bytes.size() == std::min<std::size_t>(item.data.size(), file_type_sample_size));
            CHECK(std::equal(guess.first_bytes.begin(), guess.first_bytes.end(), item.data.begin()));
        }
    }

    const auto empty = dir / "empty.bin";
    write_bytes(empty, {});
    encode_file(empty, dir / "empty.poem");
    CHECK(identify_file(dir / "empty.poem").description == "Empty file");

    CHECK(identify_file(dir / "bigpng.poem").method == CompressionMethod::Zstd);
}

TEST(identify_rejects_what_is_not_a_poem) {
    const auto dir = test::fresh_dir("identify_bad");
    write_bytes(dir / "junk.poem", bytes_of("this is not a poem at all\n"));
    CHECK_THROWS(identify_file(dir / "junk.poem"));
    CHECK_THROWS(identify_file(dir / "missing.poem"));
}

TEST(force_replaces_an_existing_output_atomically) {
    const auto dir = test::fresh_dir("force");
    const auto first = dir / "first.bin";
    const auto second = dir / "second.bin";
    const auto poem = dir / "out.poem";
    const auto restored = dir / "restored.bin";

    write_bytes(first, pseudo_random(2000, 1));
    write_bytes(second, pseudo_random(3000, 2));

    encode_file(first, poem);
    const auto old_poem = test::read_all(poem);

    // Without --force the existing output is protected and untouched.
    CHECK_THROWS_WITH(encode_file(second, poem), "--force");
    CHECK(test::read_all(poem) == old_poem);

    // With --force it is replaced by the new, complete result.
    encode_file(second, poem, CompressionMode::Auto, default_max_input, true);
    CHECK(test::read_all(poem) != old_poem);

    decode_file(poem, restored);
    CHECK(test::files_equal(second, restored));

    CHECK_THROWS_WITH(decode_file(poem, restored), "--force");
    CHECK(test::files_equal(second, restored));
    decode_file(poem, restored, default_max_output, true);
    CHECK(test::files_equal(second, restored));

    CHECK(test::count_entries(dir) == 4);  // first, second, out.poem, restored.bin - no leftovers
}

TEST(a_failed_forced_write_keeps_the_old_output) {
    const auto dir = test::fresh_dir("force_failure");
    const auto input = dir / "in.bin";
    const auto poem = dir / "in.poem";
    const auto existing = dir / "existing.bin";

    write_bytes(input, pseudo_random(5000));
    encode_file(input, poem);
    write_bytes(existing, bytes_of("precious data"));

    // Damage the poem, then try to decode it over an existing file with --force.
    auto text = read_text(poem);
    text.resize(text.size() / 2);
    write_text_file(dir / "cut.poem", text);

    CHECK_THROWS(decode_file(dir / "cut.poem", existing, default_max_output, true));
    CHECK(test::read_all(existing) == bytes_of("precious data"));
    CHECK(test::count_entries(dir) == 4);  // in.bin, in.poem, cut.poem, existing.bin

    // Same for encode over an existing file when the input is missing.
    CHECK_THROWS(encode_file(dir / "missing.bin", existing, CompressionMode::Auto, default_max_input, true));
    CHECK(test::read_all(existing) == bytes_of("precious data"));
}

TEST(the_output_can_never_be_the_input_file) {
    const auto dir = test::fresh_dir("same_file");
    const auto path = dir / "data.bin";
    write_bytes(path, pseudo_random(1000));
    const auto before = test::read_all(path);

    for (const bool force : { false, true }) {
        CHECK_THROWS_WITH(
            encode_file(path, path, CompressionMode::Auto, default_max_input, force),
            "same as the input");
        CHECK_THROWS_WITH(
            decode_file(path, path, default_max_output, force), "same as the input");
    }

    // Also through a different spelling of the same path.
    CHECK_THROWS_WITH(
        encode_file(path, dir / ".." / dir.filename() / "data.bin", CompressionMode::Auto, default_max_input, true),
        "same as the input");

    CHECK(test::read_all(path) == before);
    CHECK(test::count_entries(dir) == 1);
}

TEST(new_commands_are_parsed) {
    const auto preview = parse({ "preview", "photo.png" });
    CHECK(preview.command == Command::Preview);
    CHECK(preview.stanzas == default_preview_stanzas);
    CHECK(parse({ "preview", "x", "--stanzas", "12" }).stanzas == 12);
    CHECK(parse({ "preview", "x", "--compression", "zstd" }).compression == CompressionMode::Zstd);
    CHECK_THROWS(parse({ "preview", "x", "--stanzas", "0" }));
    CHECK_THROWS(parse({ "preview", "x", "--stanzas", "10001" }));
    CHECK_THROWS(parse({ "preview", "x", "--stanzas", "many" }));
    CHECK_THROWS(parse({ "preview", "x", "-o", "out" }));
    CHECK_THROWS(parse({ "preview", "x", "--quick" }));

    const auto estimate = parse({ "estimate", "photo.png", "--quick", "-o", "D:\\out.poem", "--max-input", "0" });
    CHECK(estimate.command == Command::Estimate);
    CHECK(estimate.quick);
    CHECK(*estimate.output == "D:\\out.poem");
    CHECK(estimate.max_input == 0);
    CHECK(!parse({ "estimate", "x" }).output.has_value());
    CHECK(!parse({ "estimate", "x" }).quick);
    CHECK_THROWS(parse({ "estimate", "x", "--stanzas", "3" }));
    CHECK_THROWS(parse({ "estimate", "x", "--force" }));

    CHECK(parse({ "identify", "a.poem" }).command == Command::Identify);
    CHECK_THROWS(parse({ "identify", "a.poem", "-o", "x" }));
    CHECK_THROWS(parse({ "identify" }));

    CHECK(parse({ "encode", "in", "-o", "out", "--force" }).force);
    CHECK(parse({ "decode", "in", "-o", "out", "--force" }).force);
    CHECK(!parse({ "encode", "in", "-o", "out" }).force);
    CHECK_THROWS(parse({ "verify", "in", "--force" }));
    CHECK_THROWS(parse({ "inspect", "in", "--force" }));
}

TEST(help_mentions_every_command_and_option) {
    const auto help = help_text();

    for (const std::string word : { "encode", "decode", "verify", "inspect", "preview", "estimate",
                                    "identify", "--force", "--stanzas", "--quick", "--compression",
                                    "--max-input", "--max-output" }) {
        CHECK(help.find(word) != std::string::npos);
    }
}

TEST(sizes_are_formatted_for_people) {
    CHECK(format_count(0) == "0");
    CHECK(format_count(999) == "999");
    CHECK(format_count(1000) == "1,000");
    CHECK(format_count(1234567) == "1,234,567");
    CHECK(format_count(1000000000) == "1,000,000,000");

    CHECK(format_size(0) == "0 bytes");
    CHECK(format_size(1023) == "1023 bytes");
    CHECK(format_size(1536) == "1.5 KiB");
    CHECK(format_size(5ull * 1024 * 1024) == "5.0 MiB");
    CHECK(format_size(3ull * 1024 * 1024 * 1024) == "3.0 GiB");
}

TEST(free_space_is_reported_also_for_a_file_that_does_not_exist_yet) {
    const auto dir = test::fresh_dir("space");

    const auto here = free_space_for(dir);
    const auto future = free_space_for(dir / "not" / "created" / "yet.poem");

    CHECK(here.has_value());
    CHECK(future.has_value());
    CHECK(*here > 0);
}
