// Stage profiler: measures the cost of the individual stages of encode and
// verify in one process, so optimization decisions rest on numbers.
//
// Build:  cmake -DBARDZIP_BUILD_TOOLS=ON ...   (target: bardzip_stage_profile)
// Run:    bardzip_stage_profile [data MiB, default 4]
//
// Throughput is reported in MiB/s of DATA, and in MiB/s of poem text where
// that is meaningful (a poem is about 54 times larger than its data).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "codec/poem_codec.hpp"
#include "container/container.hpp"
#include "container/crc32.hpp"
#include "core/file_operations.hpp"
#include "io/file_reader.hpp"

using namespace bardzip;
using Clock = std::chrono::steady_clock;

namespace {

struct Row {
    std::string name;
    double seconds;
    double data_mib;
    double poem_mib;
};

std::vector<Row> rows;

template <typename F>
double best_of(int runs, F&& action) {
    double best = 1e300;
    for (int i = 0; i < runs; ++i) {
        const auto start = Clock::now();
        action();
        best = (std::min)(best, std::chrono::duration<double>(Clock::now() - start).count());
    }
    return best;
}

void report(const std::string& name, double seconds, double data_mib, double poem_mib) {
    rows.push_back({ name, seconds, data_mib, poem_mib });
}

std::vector<std::uint8_t> random_data(std::size_t size) {
    std::vector<std::uint8_t> data(size);
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    for (auto& byte : data) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        byte = static_cast<std::uint8_t>(state >> 32);
    }
    return data;
}

}

int main(int argc, char* argv[]) {
    const std::size_t data_mib = argc > 1 ? std::stoul(argv[1]) : 4;
    const std::size_t size = data_mib * 1024 * 1024;
    constexpr int runs = 3;

    const auto data = random_data(size);
    const double mib = static_cast<double>(size) / (1024.0 * 1024.0);

    const auto container = build_container(data, CompressionMode::None);
    const auto poem = encode_poem(container);
    const double poem_mib = static_cast<double>(poem.size()) / (1024.0 * 1024.0);
    const std::size_t stanzas = container.size() / 2;

    std::size_t sink = 0;  // keeps the optimizer from removing the work

    // 1. CRC32 over the data (encode computes it twice, verify three times).
    report("crc32 over data", best_of(runs, [&] { sink += crc32(data); }), mib, 0);

    // 2. PoemEncoder: bytes -> poem text, text discarded.
    report("PoemEncoder (bytes -> text, null sink)", best_of(runs, [&] {
        PoemEncoder encoder([&](std::string_view text) { sink += text.size(); });
        encoder.write(data);
        encoder.finish();
    }), mib, poem_mib);

    // 3. decode_stanza alone on a small, cache-resident set of stanzas, repeated
    // to the full count. (A big heap-allocated set would measure cache misses.)
    {
        constexpr std::size_t sample = 2000;
        std::vector<Stanza> prepared;
        prepared.reserve(sample);
        for (std::size_t i = 0; i < sample; ++i) {
            prepared.push_back(encode_stanza(data[(2 * i) % size], data[(2 * i + 1) % size]));
        }

        const std::size_t repeats = stanzas / sample;

        const double total = best_of(runs, [&] {
            for (std::size_t r = 0; r < repeats; ++r) {
                for (const auto& stanza : prepared) {
                    sink += decode_stanza(stanza)[0];
                }
            }
        });

        report("decode_stanza only (hot cache)", total, mib, poem_mib);
    }

    // 4. decode_poem_stream from memory, payload discarded (stream copy subtracted).
    {
        const double total = best_of(runs, [&] {
            std::istringstream stream(poem);
            decode_poem_stream(stream, {}, [&](std::span<const std::uint8_t> chunk) { sink += chunk.size(); });
        });
        const double copy = best_of(runs, [&] {
            std::istringstream stream(poem);
            sink += static_cast<std::size_t>(stream.tellg() == 0);
        });
        report("decode_poem_stream from memory (minus stream copy)", total - copy, mib, poem_mib);
    }

    // Poem and data on disk for the file based stages.
    const auto dir = std::filesystem::temp_directory_path() / "bardzip_stage_profile";
    std::filesystem::create_directories(dir);
    const auto poem_path = dir / "profile.poem";
    const auto input_path = dir / "profile.bin";
    {
        std::ofstream out(poem_path, std::ios::binary | std::ios::trunc);
        out.write(poem.data(), static_cast<std::streamsize>(poem.size()));
        std::ofstream in(input_path, std::ios::binary | std::ios::trunc);
        in.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(size));
    }

    // 5. Reading the poem file in 64 KiB blocks (warm cache): the I/O floor.
    report("read poem file in 64 KiB blocks", best_of(runs, [&] {
        auto in = open_input_file(poem_path);
        std::vector<std::uint8_t> block(64 * 1024);
        while (const auto count = read_block(in, block)) {
            sink += count;
        }
    }), mib, poem_mib);

    // 6. decode_poem_stream from the file, payload discarded.
    report("decode_poem_stream from file", best_of(runs, [&] {
        auto in = open_input_file(poem_path);
        decode_poem_stream(in, {}, [&](std::span<const std::uint8_t> chunk) { sink += chunk.size(); });
    }), mib, poem_mib);

    // 7. The real commands.
    std::cout.setstate(std::ios::failbit);  // verify_file prints a line
    report("verify_file (whole command)", best_of(runs, [&] { verify_file(poem_path); }), mib, poem_mib);
    std::cout.clear();

    const auto encoded = dir / "profile.encoded.poem";
    report("encode_file (whole command, no compression)", best_of(runs, [&] {
        std::filesystem::remove(encoded);
        encode_file(input_path, encoded, CompressionMode::None, 0);
    }), mib, poem_mib);

    std::filesystem::remove_all(dir);

    std::cout << "Data: " << data_mib << " MiB random, poem " << std::fixed << std::setprecision(0)
              << poem_mib << " MiB (" << stanzas << " stanzas), best of " << runs << " runs\n\n"
              << "| Stage | Seconds | Data MiB/s | Poem MiB/s |\n|---|---:|---:|---:|\n";

    for (const auto& row : rows) {
        std::cout << "| " << row.name << " | " << std::setprecision(3) << row.seconds
                  << " | " << std::setprecision(1) << row.data_mib / row.seconds << " | ";
        if (row.poem_mib > 0) {
            std::cout << std::setprecision(0) << row.poem_mib / row.seconds;
        }
        else {
            std::cout << "-";
        }
        std::cout << " |\n";
    }

    std::cout << "\n(checksum to defeat dead-code elimination: " << sink << ")\n";
}
