#include "cli/arguments.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "bardzip_version.hpp"
#include "cli/commands.hpp"
#include "core/errors.hpp"

namespace bardzip {

namespace {

constexpr std::string_view usage =
    "Usage:\n"
    "  bardzip encode <input> -o <output> [--compression none|zstd|auto]"
    " [--max-input <bytes>] [--force]\n"
    "  bardzip decode <input> -o <output> [--max-output <bytes>] [--force]\n"
    "  bardzip verify <input> [--max-output <bytes>]\n"
    "  bardzip inspect <input>\n"
    "  bardzip preview <input> [--stanzas <n>] [--compression none|zstd|auto]\n"
    "  bardzip estimate <input> [--compression none|zstd|auto] [--quick]"
    " [--max-input <bytes>] [-o <output>]\n"
    "  bardzip identify <poem>\n"
    "  bardzip completion powershell\n"
    "  bardzip --help | --version\n"
    "Every command also accepts -q/--quiet and -v/--verbose.\n";

constexpr std::string_view help_details =
    "\n"
    "Turns any file into a Russian poem and back, byte for byte.\n"
    "\n"
    "Commands:\n"
    "  encode      Turn <input> into a poem written to <output>.\n"
    "  decode      Restore the original file from a poem.\n"
    "  verify      Check a poem completely (all checksums); writes nothing.\n"
    "  inspect     Print the container header. Does not check the payload.\n"
    "  preview     Show the first stanzas of the poem that encode would write,\n"
    "              and its exact size. Writes nothing.\n"
    "  estimate    Tell how large the poem would be and whether it fits on the\n"
    "              disk. Exact by default (reads the file once); --quick only\n"
    "              looks at the file size and gives a range. Writes nothing.\n"
    "  identify    Guess the type of the file hidden in a poem (PNG, JPEG, ZIP,\n"
    "              PDF ...) from its first bytes. Does not verify the poem.\n"
    "  completion  Print a tab-completion script. Install it in PowerShell with:\n"
    "                bardzip completion powershell | Out-String | Invoke-Expression\n"
    "\n"
    "Options:\n"
    "  -o <path>                  Output file. It must not exist yet unless\n"
    "                             --force is given. For estimate: the file whose\n"
    "                             disk is checked for free space (default: the\n"
    "                             current folder).\n"
    "  --force                    encode, decode. Replace an existing output file\n"
    "                             (only once the new one is complete). The output\n"
    "                             can never be the input file itself.\n"
    "  --compression none|zstd|auto\n"
    "                             encode, preview, estimate. Default: auto (zstd,\n"
    "                             but stored as is when it would not make data\n"
    "                             smaller).\n"
    "  --max-input <bytes>        encode, estimate. Refuse inputs larger than this.\n"
    "                             Default: 16 MiB. 0 = no limit.\n"
    "  --max-output <bytes>       decode and verify. Refuse poems that would\n"
    "                             restore more than this. Default: 256 MiB.\n"
    "  --stanzas <n>              preview. How many stanzas to show. Default: 6.\n"
    "  --quick                    estimate. Approximate size from the file size only.\n"
    "  -q, --quiet                Print only errors and the results you asked for\n"
    "                             (no report, no progress line).\n"
    "  -v, --verbose              Also print timings and the effective settings.\n"
    "  -h, --help                 Show this help.\n"
    "  -V, --version              Show the version.\n"
    "\n"
    "Standard input and output:\n"
    "  Use - instead of a file name: bardzip encode - -o - reads standard input and\n"
    "  writes the poem to standard output, so it works in pipelines. Standard input\n"
    "  is first copied to a temporary file (the encoder needs its exact size).\n"
    "  decode refuses to write binary data to a terminal. A file really named - can\n"
    "  be given as .\\-\n"
    "\n"
    "Progress: while a long operation runs and the screen is a console, a line such\n"
    "as \"Writing poem  42%  1.2 GiB written  ~40 s left\" is shown on stderr.\n"
    "\n"
    "Exit codes:\n"
    "  0  success\n"
    "  1  internal error\n"
    "  2  wrong usage (bad command line)\n"
    "  3  file or system problem (cannot open or write, output exists, disk full)\n"
    "  4  bad data (not a poem, damaged, checksum mismatch, unsupported version)\n"
    "  5  limit exceeded (--max-input / --max-output)\n"
    "\n"
    "Good to know:\n"
    "  * A poem is about 54 times larger than the data it carries.\n"
    "  * This is not encryption. CRC32 detects accidental damage only and\n"
    "    does not authenticate the data.\n";
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

    throw UsageError(
        "Unknown compression mode: " + std::string(value)
        + " (expected none, zstd or auto)"
    );
}

std::uint64_t parse_size(std::string_view value) {
    std::uint64_t result = 0;
    const auto* end = value.data() + value.size();
    const auto [pointer, error] = std::from_chars(value.data(), end, result);

    if (value.empty() || error != std::errc{} || pointer != end) {
        throw UsageError(
            "Invalid size: " + std::string(value)
        );
    }

    return result;
}

}  // namespace

std::string help_text() {
    return "bardzip " + std::string(BARDZIP_VERSION) + "\n\n"
        + std::string(usage) + std::string(help_details);
}

std::string version_text() {
    return "bardzip " + std::string(BARDZIP_VERSION) + "\n";
}

Arguments parse_arguments(int argc, char* argv[]) {
    if (argc < 2) {
        throw UsageError(std::string(usage));
    }

    Arguments arguments;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];

        if (argument == "--help" || argument == "-h") {
            arguments.command = Command::Help;
            return arguments;
        }
    }

    const std::string_view command = argv[1];

    if (command == "help") {
        arguments.command = Command::Help;
        return arguments;
    }

    if (command == "--version" || command == "-V" || command == "version") {
        arguments.command = Command::Version;
        return arguments;
    }

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
    else if (command == "preview") {
        arguments.command = Command::Preview;
    }
    else if (command == "estimate") {
        arguments.command = Command::Estimate;
    }
    else if (command == "identify") {
        arguments.command = Command::Identify;
    }
    else if (command == "completion") {
        arguments.command = Command::Completion;
    }
    else {
        throw UsageError("Unknown command: " + std::string(command));
    }

    const bool writes_output =
        arguments.command == Command::Encode ||
        arguments.command == Command::Decode;
    const bool reads_poem_limit =
        arguments.command == Command::Decode ||
        arguments.command == Command::Verify;
    const bool analyzes_file =
        arguments.command == Command::Preview ||
        arguments.command == Command::Estimate;

    const std::vector<std::string_view> args(argv + 2, argv + argc);

    bool input_set = false;
    bool quiet = false;
    bool verbose = false;

    const auto value_of = [&](std::size_t& index) {
        if (index + 1 >= args.size()) {
            throw UsageError(
                "Missing value for " + std::string(args[index])
            );
        }

        return args[++index];
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto argument = args[i];

        if (argument == "-o" &&
            (writes_output || arguments.command == Command::Estimate))
        {
            if (arguments.output) {
                throw UsageError("-o specified more than once");
            }

            arguments.output = std::filesystem::path(value_of(i));
        }
        else if (argument == "--compression" &&
                 (arguments.command == Command::Encode || analyzes_file))
        {
            arguments.compression = parse_compression(value_of(i));
        }
        else if (argument == "--max-input" &&
                 (arguments.command == Command::Encode ||
                  arguments.command == Command::Estimate))
        {
            arguments.max_input = parse_size(value_of(i));
        }
        else if (argument == "--max-output" && reads_poem_limit) {
            arguments.max_output = parse_size(value_of(i));
        }
        else if (argument == "--force" && writes_output) {
            arguments.force = true;
        }
        else if (argument == "--quick" && arguments.command == Command::Estimate) {
            arguments.quick = true;
        }
        else if (argument == "--stanzas" && arguments.command == Command::Preview) {
            arguments.stanzas = parse_size(value_of(i));

            if (arguments.stanzas == 0 || arguments.stanzas > max_preview_stanzas) {
                throw UsageError(
                    "--stanzas must be between 1 and "
                    + std::to_string(max_preview_stanzas)
                );
            }
        }
        else if (argument == "-q" || argument == "--quiet") {
            quiet = true;
        }
        else if (argument == "-v" || argument == "--verbose") {
            verbose = true;
        }
        else if (argument.starts_with('-') && argument.size() > 1) {
            throw UsageError(
                "Unknown or unsupported option: " + std::string(argument)
            );
        }
        else if (input_set) {
            throw UsageError(
                "Unexpected argument: " + std::string(argument)
            );
        }
        else {
            arguments.input = argument;
            input_set = true;
        }
    }

    if (quiet && verbose) {
        throw UsageError("--quiet and --verbose cannot be used together");
    }

    arguments.verbosity = quiet ? Verbosity::Quiet
        : verbose ? Verbosity::Verbose
        : Verbosity::Normal;

    if (!input_set || arguments.input.empty()) {
        throw UsageError("Input path is required\n" + std::string(usage));
    }

    if (arguments.command == Command::Completion) {
        arguments.shell = arguments.input.string();
        const auto& shells = completion_shells();

        if (std::find(shells.begin(), shells.end(), arguments.shell) == shells.end()) {
            throw UsageError(
                "Unknown shell: " + arguments.shell + " (supported: powershell)"
            );
        }
    }

    if (arguments.command == Command::Estimate && arguments.quick &&
        arguments.input == std::filesystem::path("-"))
    {
        throw UsageError("--quick needs a file: the size of standard input is not known");
    }

    if (writes_output && !arguments.output) {
        throw UsageError("Output path is required (-o)");
    }

    if (arguments.output && arguments.output->empty()) {
        throw UsageError("Output path cannot be empty");
    }

    return arguments;
}

}
