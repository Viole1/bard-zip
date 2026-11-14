#include <string>
#include <vector>

#include "cli/arguments.hpp"
#include "test_support.hpp"

using namespace bardzip;

namespace {

Arguments parse(std::vector<std::string> args) {
    args.insert(args.begin(), "bardzip");

    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }

    return parse_arguments(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST(encode_compression_modes) {
    CHECK(parse({ "encode", "in.zip", "-o", "out.poem" }).compression == CompressionMode::Auto);

    CHECK(parse({ "encode", "in.zip", "-o", "out.poem", "--compression", "none" })
              .compression == CompressionMode::None);
    CHECK(parse({ "encode", "in.zip", "-o", "out.poem", "--compression", "zstd" })
              .compression == CompressionMode::Zstd);
    CHECK(parse({ "encode", "in.zip", "-o", "out.poem", "--compression", "auto" })
              .compression == CompressionMode::Auto);
}

TEST(encode_accepts_options_in_any_order) {
    const auto arguments =
        parse({ "encode", "--compression", "zstd", "-o", "out.poem", "in.zip" });

    CHECK(arguments.command == Command::Encode);
    CHECK(arguments.input == "in.zip");
    CHECK(*arguments.output == "out.poem");
    CHECK(arguments.compression == CompressionMode::Zstd);
}

TEST(encode_rejects_bad_compression_usage) {
    CHECK_THROWS_WITH(parse({ "encode", "in", "-o", "out", "--compression", "gzip" }),
                      "Unknown compression mode");
    CHECK_THROWS_WITH(parse({ "encode", "in", "-o", "out", "--compression" }),
                      "Missing value");
    CHECK_THROWS(parse({ "decode", "in", "-o", "out", "--compression", "zstd" }));
    CHECK_THROWS(parse({ "verify", "in", "--compression", "zstd" }));
}

TEST(max_output_parsing) {
    CHECK(parse({ "decode", "in", "-o", "out" }).max_output == default_max_output);
    CHECK(parse({ "decode", "in", "-o", "out", "--max-output", "1000" }).max_output == 1000);
    CHECK(parse({ "verify", "in", "--max-output", "42" }).max_output == 42);

    CHECK_THROWS(parse({ "verify", "in", "--max-output", "abc" }));
    CHECK_THROWS(parse({ "verify", "in", "--max-output", "-1" }));
    CHECK_THROWS(parse({ "verify", "in", "--max-output", "12x" }));
    CHECK_THROWS(parse({ "encode", "in", "-o", "out", "--max-output", "10" }));
}

TEST(max_input_parsing) {
    CHECK(parse({ "encode", "in", "-o", "out" }).max_input == default_max_input);
    CHECK(default_max_input == 32ULL * 1024 * 1024);
    CHECK(parse({ "encode", "in", "-o", "out", "--max-input", "1000" }).max_input == 1000);
    CHECK(parse({ "encode", "in", "-o", "out", "--max-input", "0" }).max_input == 0);

    CHECK_THROWS(parse({ "encode", "in", "-o", "out", "--max-input", "abc" }));
    CHECK_THROWS(parse({ "encode", "in", "-o", "out", "--max-input" }));
    CHECK_THROWS(parse({ "decode", "in", "-o", "out", "--max-input", "10" }));
    CHECK_THROWS(parse({ "verify", "in", "--max-input", "10" }));
}

TEST(required_arguments) {
    CHECK_THROWS(parse({}));
    CHECK_THROWS(parse({ "encode", "in" }));
    CHECK_THROWS(parse({ "encode", "-o", "out" }));
    CHECK_THROWS(parse({ "encode", "a", "b", "-o", "out" }));
    CHECK_THROWS(parse({ "inspect", "in", "-o", "out" }));
    CHECK_THROWS(parse({ "compress", "in" }));
    CHECK(parse({ "inspect", "in" }).command == Command::Inspect);
}
