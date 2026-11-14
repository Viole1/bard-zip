#include "cli/arguments.hpp"

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bardzip {

namespace {

constexpr std::string_view usage =
    "Usage:\n"
    "  bardzip encode <input> -o <output> [--compression none|zstd|auto]"
    " [--max-input <bytes>]\n"
    "  bardzip decode <input> -o <output> [--max-output <bytes>]\n"
    "  bardzip verify <input> [--max-output <bytes>]\n"
    "  bardzip inspect <input>";

CompressionMode parse_compression(std::string_view value) {
    if (value == "none") {
        return CompressionMode::None;
    }

    if (value == "zstd") {
        return CompressionMode::Zstd;
    }

    if (value == "auto") {
        return CompressionMode::Auto;
    }

    throw std::invalid_argument(
        "Unknown compression mode: " + std::string(value)
        + " (expected none, zstd or auto)"
    );
}

std::uint64_t parse_size(std::string_view value) {
    std::uint64_t result = 0;
    const auto* end = value.data() + value.size();
    const auto [pointer, error] = std::from_chars(value.data(), end, result);

    if (value.empty() || error != std::errc{} || pointer != end) {
        throw std::invalid_argument(
            "Invalid size: " + std::string(value)
        );
    }

    return result;
}

}  // namespace

Arguments parse_arguments(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::invalid_argument(std::string(usage));
    }

    Arguments arguments;

    const std::string_view command = argv[1];

    if (command == "encode") {
        arguments.command = Command::Encode;
    }
    else if (command == "decode") {
        arguments.command = Command::Decode;
    }
    else if (command == "inspect") {
        arguments.command = Command::Inspect;
    }
    else if (command == "verify") {
        arguments.command = Command::Verify;
    }
    else {
        throw std::invalid_argument("Unknown command: " + std::string(command));
    }

    const bool writes_output =
        arguments.command == Command::Encode ||
        arguments.command == Command::Decode;
    const bool reads_poem_limit =
        arguments.command == Command::Decode ||
        arguments.command == Command::Verify;

    const std::vector<std::string_view> args(argv + 2, argv + argc);

    bool input_set = false;

    const auto value_of = [&](std::size_t& index) {
        if (index + 1 >= args.size()) {
            throw std::invalid_argument(
                "Missing value for " + std::string(args[index])
            );
        }

        return args[++index];
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto argument = args[i];

        if (argument == "-o" && writes_output) {
            if (arguments.output) {
                throw std::invalid_argument("-o specified more than once");
            }

            arguments.output = std::filesystem::path(value_of(i));
        }
        else if (argument == "--compression" &&
                 arguments.command == Command::Encode)
        {
            arguments.compression = parse_compression(value_of(i));
        }
        else if (argument == "--max-input" &&
                 arguments.command == Command::Encode)
        {
            arguments.max_input = parse_size(value_of(i));
        }
        else if (argument == "--max-output" && reads_poem_limit) {
            arguments.max_output = parse_size(value_of(i));
        }
        else if (argument.starts_with('-') && argument.size() > 1) {
            throw std::invalid_argument(
                "Unknown or unsupported option: " + std::string(argument)
            );
        }
        else if (input_set) {
            throw std::invalid_argument(
                "Unexpected argument: " + std::string(argument)
            );
        }
        else {
            arguments.input = argument;
            input_set = true;
        }
    }

    if (!input_set || arguments.input.empty()) {
        throw std::invalid_argument("Input path is required\n" + std::string(usage));
    }

    if (writes_output && !arguments.output) {
        throw std::invalid_argument("Output path is required (-o)");
    }

    if (arguments.output && arguments.output->empty()) {
        throw std::invalid_argument("Output path cannot be empty");
    }

    return arguments;
}

}
