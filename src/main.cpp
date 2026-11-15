#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "bardzip_version.hpp"
#include "cli/arguments.hpp"
#include "cli/commands.hpp"
#include "cli/console_progress.hpp"
#include "cli/exit_codes.hpp"
#include "codec/poem_codec.hpp"
#include "container/header.hpp"
#include "core/errors.hpp"
#include "core/file_operations.hpp"
#include "core/size_format.hpp"
#include "io/stdio.hpp"

namespace {

using bardzip::Verbosity;
using bardzip::format_count;
using bardzip::format_size;

using Clock = std::chrono::steady_clock;

struct Context {
    const bardzip::Arguments& arguments;
    bardzip::ProgressObserver* progress = nullptr;

    bool quiet() const { return arguments.verbosity == Verbosity::Quiet; }
    bool verbose() const { return arguments.verbosity == Verbosity::Verbose; }
};

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string describe_size(std::uint64_t bytes) {
    return format_count(bytes) + " bytes (" + format_size(bytes) + ")";
}

std::string describe_speed(std::uint64_t bytes, double seconds) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << seconds << " s";

    if (seconds > 0.0) {
        text << " (" << std::setprecision(1)
             << static_cast<double>(bytes) / (1024.0 * 1024.0) / seconds << " MiB/s)";
    }

    return text.str();
}

void print_report(const Context& context, const bardzip::EncodeReport& report) {
    if (context.quiet()) {
        return;
    }

    std::cerr
        << "Compression method: " << bardzip::to_string(report.method) << '\n'
        << "Original bytes:     " << report.original_size << '\n'
        << "Payload bytes:      " << report.payload_size << '\n'
        << "Container bytes:    " << report.container_size << '\n'
        << "Poem bytes:         " << report.poem_size << '\n'
        << "Stanzas:            " << report.stanza_count << '\n';

    // The default decode limit is lower than an explicitly raised encode limit.
    if (report.original_size > bardzip::default_max_output) {
        std::cerr
            << "Note: decoding this poem needs --max-output "
            << report.original_size << " (or more).\n";
    }
}

void run_encode(const Context& context) {
    const auto& arguments = context.arguments;
    const auto start = Clock::now();

    if (context.verbose()) {
        std::cerr << "encode: " << arguments.input.string() << " -> " << arguments.output->string()
                  << " (compression " << (arguments.compression == bardzip::CompressionMode::None ? "none"
                      : arguments.compression == bardzip::CompressionMode::Zstd ? "zstd" : "auto")
                  << ", input limit "
                  << (arguments.max_input == 0 ? std::string("none") : format_size(arguments.max_input))
                  << (arguments.force ? ", --force" : "") << ")\n";
    }

    const auto report = bardzip::encode_file(
        arguments.input,
        *arguments.output,
        arguments.compression,
        arguments.max_input,
        arguments.force,
        context.progress
    );

    print_report(context, report);

    if (context.verbose()) {
        std::cerr << "Finished: " << describe_size(report.original_size) << " became a poem of "
                  << describe_size(report.poem_size) << " in "
                  << describe_speed(report.original_size, seconds_since(start)) << " of input\n";
    }
}

void run_decode(const Context& context) {
    const auto& arguments = context.arguments;
    const auto start = Clock::now();

    if (context.verbose()) {
        std::cerr << "decode: " << arguments.input.string() << " -> " << arguments.output->string()
                  << " (output limit " << format_size(arguments.max_output)
                  << (arguments.force ? ", --force" : "") << ")\n";
    }

    const auto report = bardzip::decode_file(
        arguments.input,
        *arguments.output,
        arguments.max_output,
        arguments.force,
        context.progress
    );

    if (context.verbose()) {
        std::cerr << "Finished: restored " << describe_size(report.original_size) << " (payload "
                  << bardzip::to_string(report.method) << ") in "
                  << describe_speed(report.original_size, seconds_since(start)) << '\n';
    }
}

void run_verify(const Context& context) {
    const auto& arguments = context.arguments;
    const auto start = Clock::now();

    const auto report = bardzip::verify_file(
        arguments.input, arguments.max_output, context.progress);

    if (!context.quiet()) {
        std::cout << "Verification successful.\n";
    }

    if (context.verbose()) {
        std::cerr << "Checked " << describe_size(report.original_size) << " of "
                  << bardzip::to_string(report.method) << " data in "
                  << describe_speed(report.original_size, seconds_since(start)) << '\n';
    }
}

// The poem text goes to stdout so it can be redirected; the notes go to stderr.
void run_preview(const Context& context) {
    const auto& arguments = context.arguments;

    const auto analysis = bardzip::analyze_file(
        arguments.input, arguments.compression, arguments.stanzas, context.progress);

    const auto shown = (std::min)(analysis.stanza_count, arguments.stanzas);

    if (!context.quiet()) {
        std::cerr << "First " << shown << " of " << format_count(analysis.stanza_count)
                  << " stanzas of the poem for " << arguments.input.string() << ":\n\n";
    }

    std::cout << analysis.head;

    if (shown < analysis.stanza_count) {
        std::cout << "... " << format_count(analysis.stanza_count - shown)
                  << " more stanzas ...\n";
    }

    if (!context.quiet()) {
        std::cerr << "\nPoem size: " << describe_size(analysis.poem_size)
                  << " (exact), compression: " << bardzip::to_string(analysis.method) << '\n';
    }
}

void print_input_limit(std::uint64_t original_size, std::uint64_t max_input) {
    if (max_input == 0) {
        std::cout << "Input limit:      none (--max-input 0)\n";
    }
    else if (original_size <= max_input) {
        std::cout << "Input limit:      within the limit of " << format_size(max_input) << '\n';
    }
    else {
        std::cout << "Input limit:      EXCEEDED (limit " << format_size(max_input)
                  << "): encode needs --max-input " << original_size << " or 0\n";
    }
}

void print_disk_space(
    const bardzip::Arguments& arguments,
    std::uint64_t smallest_needed,
    std::uint64_t largest_needed
) {
    const auto where = arguments.output ? *arguments.output : std::filesystem::path(".");
    const auto free = bardzip::free_space_for(bardzip::is_stdio(where) ? std::filesystem::path(".") : where);

    if (!free) {
        std::cout << "Disk space:       could not be determined\n";
        return;
    }

    std::cout << "Free disk space:  " << format_size(*free) << " on the volume of "
              << std::filesystem::absolute(where).string() << '\n';

    if (*free < smallest_needed) {
        std::cout << "Result:           NOT ENOUGH SPACE\n";
    }
    else if (*free < largest_needed) {
        std::cout << "Result:           probably enough, but not guaranteed (see the range)\n";
    }
    else {
        std::cout << "Result:           enough space\n";
    }
}

void run_estimate(const Context& context) {
    const auto& arguments = context.arguments;

    std::cout << "Input:            " << arguments.input.string() << '\n';

    if (arguments.quick) {
        const auto size = std::filesystem::file_size(arguments.input);
        const auto bounds = bardzip::poem_size_bounds(bardzip::header_size + size);

        std::cout << "Size:             " << describe_size(size) << '\n'
                  << "Stanzas:          " << format_count(bounds.stanzas) << '\n'
                  << "Poem size:        about " << format_size(bounds.typical)
                  << ", between " << format_size(bounds.smallest) << " and "
                  << format_size(bounds.largest) << " - APPROXIMATE\n"
                  << "                  (assumes no compression and random data; "
                     "run without --quick for the exact size)\n";

        print_input_limit(size, arguments.max_input);
        print_disk_space(arguments, bounds.smallest, bounds.largest);
        return;
    }

    const auto analysis = bardzip::analyze_file(
        arguments.input, arguments.compression, 0, context.progress);

    std::cout << "Size:             " << describe_size(analysis.original_size) << '\n'
              << "Compression:      " << bardzip::to_string(analysis.method);

    if (analysis.method == bardzip::CompressionMethod::Zstd) {
        std::cout << " (payload " << describe_size(analysis.payload_size) << ')';
    }

    // While compressing, a temporary payload file is kept next to the output.
    const auto scratch = analysis.method == bardzip::CompressionMethod::Zstd
        ? analysis.payload_size : 0;

    std::cout << '\n'
              << "Container:        " << describe_size(analysis.container_size) << '\n'
              << "Stanzas:          " << format_count(analysis.stanza_count) << '\n'
              << "Poem size:        " << describe_size(analysis.poem_size) << " - EXACT\n";

    print_input_limit(analysis.original_size, arguments.max_input);
    print_disk_space(arguments, analysis.poem_size + scratch, analysis.poem_size + scratch);
}

void run_identify(const Context& context) {
    const auto guess = bardzip::identify_file(context.arguments.input);

    std::cout << "Hidden file:      " << describe_size(guess.original_size) << ", "
              << (guess.method == bardzip::CompressionMethod::Zstd
                      ? "compressed with zstd" : "stored without compression") << '\n'
              << "Looks like:       " << guess.description;

    if (!guess.extension.empty()) {
        std::cout << " (." << guess.extension << ')';
    }

    std::cout << "\nFirst bytes:      ";

    std::ostringstream hex;
    hex << std::hex << std::uppercase << std::setfill('0');

    for (std::size_t i = 0; i < (std::min<std::size_t>)(guess.first_bytes.size(), 16); ++i) {
        hex << (i ? " " : "") << std::setw(2) << static_cast<int>(guess.first_bytes[i]);
    }

    std::cout << hex.str() << '\n';

    if (!context.quiet()) {
        std::cout << "\nThis is a guess from the first bytes; the poem was not verified. "
                     "Use `bardzip verify` to check it, then `bardzip decode`"
                  << (guess.extension.empty() ? "" : (" with -o file." + guess.extension))
                  << " to restore the file.\n";
    }
}

bool shows_progress(const bardzip::Arguments& arguments) {
    switch (arguments.command) {
    case bardzip::Command::Encode:
    case bardzip::Command::Decode:
    case bardzip::Command::Verify:
    case bardzip::Command::Preview:
    case bardzip::Command::Estimate:
        return arguments.verbosity != Verbosity::Quiet && bardzip::is_console(2);

    default:
        return false;
    }
}

int run(int argc, char* argv[]) {
    const auto arguments = bardzip::parse_arguments(argc, argv);

    // The line is erased whenever a stage ends and when this object is destroyed,
    // so it never mixes with the output that follows.
    std::unique_ptr<bardzip::ConsoleProgress> progress;

    if (shows_progress(arguments)) {
        progress = std::make_unique<bardzip::ConsoleProgress>(std::cerr);
    }

    const Context context{ arguments, progress.get() };

    switch (arguments.command) {
    case bardzip::Command::Encode:
        run_encode(context);
        break;

    case bardzip::Command::Decode:
        run_decode(context);
        break;

    case bardzip::Command::Inspect:
        bardzip::inspect_file(arguments.input);
        break;

    case bardzip::Command::Verify:
        run_verify(context);
        break;

    case bardzip::Command::Preview:
        run_preview(context);
        break;

    case bardzip::Command::Estimate:
        run_estimate(context);
        break;

    case bardzip::Command::Identify:
        run_identify(context);
        break;

    case bardzip::Command::Completion:
        std::cout << bardzip::completion_script(arguments.shell);
        break;

    case bardzip::Command::Help:
        std::cout << bardzip::help_text();
        break;

    case bardzip::Command::Version:
        std::cout << bardzip::version_text();
        break;
    }

    return static_cast<int>(bardzip::ExitCode::Success);
}

}

int main(int argc, char* argv[]) {
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        verbose = verbose || argument == "-v" || argument == "--verbose";
    }

    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif

        return run(argc, argv);
    }
    catch (const std::exception& error) {
        const auto code = bardzip::exit_code_for(error);

        std::cerr << "Error: " << error.what() << '\n';

        if (code == bardzip::ExitCode::Usage &&
            std::string_view(error.what()).find("Usage:") == std::string_view::npos)
        {
            std::cerr << "Try 'bardzip --help' for usage.\n";
        }

        if (verbose) {
            std::cerr << "[exit code " << static_cast<int>(code) << ": "
                      << bardzip::describe(code) << "]\n";
        }

        return static_cast<int>(code);
    }
}
