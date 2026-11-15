#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <ios>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include "cli/arguments.hpp"
#include "cli/commands.hpp"
#include "cli/console_progress.hpp"
#include "cli/exit_codes.hpp"
#include "core/errors.hpp"
#include "core/file_operations.hpp"
#include "io/file_writer.hpp"
#include "io/stdio.hpp"
#include "test_support.hpp"

using namespace bardzip;

namespace {

std::vector<std::uint8_t> pseudo_random(std::size_t size, std::uint32_t seed = 5) {
    std::vector<std::uint8_t> data(size);
    for (auto& byte : data) {
        seed = seed * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(seed >> 24);
    }
    return data;
}

// Compressible data, so that auto mode picks zstd.
std::vector<std::uint8_t> repetitive(std::size_t size) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::uint8_t>(i % 11);
    }
    return data;
}

Arguments parse(std::vector<std::string> args) {
    args.insert(args.begin(), "bardzip");
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    return parse_arguments(static_cast<int>(argv.size()), argv.data());
}

// Temporarily points standard input or output at a file.
class Redirect {
public:
    Redirect(int fd, const std::filesystem::path& file, bool reading) : fd_(fd) {
        std::fflush(stdout);
        saved_ = _dup(fd);

        const auto path = file.string();
        const int opened = reading
            ? _open(path.c_str(), _O_RDONLY | _O_BINARY)
            : _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);

        if (saved_ < 0 || opened < 0) {
            throw std::runtime_error("Cannot redirect a standard stream in the test");
        }

        _dup2(opened, fd);
        _close(opened);
    }

    ~Redirect() {
        std::fflush(stdout);
        _dup2(saved_, fd_);
        _close(saved_);
        std::clearerr(stdin);
    }

    Redirect(const Redirect&) = delete;
    Redirect& operator=(const Redirect&) = delete;

private:
    int fd_;
    int saved_ = -1;
};

struct Recorded {
    std::string stage;
    std::uint64_t total = 0;
    std::uint64_t last_done = 0;
    std::uint64_t last_produced = 0;
    bool monotonic = true;
    bool ended = false;
};

class Recorder : public ProgressObserver {
public:
    void begin(std::string_view stage, std::uint64_t total) override {
        stages.push_back({ std::string(stage), total });
        ++open_stages;
    }

    void advance(std::uint64_t done, std::uint64_t produced) override {
        auto& stage = stages.back();
        stage.monotonic = stage.monotonic && done >= stage.last_done;
        stage.last_done = done;
        stage.last_produced = produced;
    }

    void end() override {
        stages.back().ended = true;
        --open_stages;
    }

    std::vector<Recorded> stages;
    int open_stages = 0;
};

std::vector<std::string> names(const Recorder& recorder) {
    std::vector<std::string> result;
    for (const auto& stage : recorder.stages) {
        result.push_back(stage.stage);
    }
    return result;
}

std::size_t spool_files_in_temp() {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path())) {
        const auto name = entry.path().filename().string();
        count += name.starts_with("bardzip-input") || name.starts_with("bardzip-output") ? 1 : 0;
    }
    return count;
}

}

TEST(exit_codes_follow_the_kind_of_error) {
    CHECK(exit_code_for(UsageError("x")) == ExitCode::Usage);
    CHECK(exit_code_for(LimitError("x")) == ExitCode::Limit);
    CHECK(exit_code_for(std::invalid_argument("damaged")) == ExitCode::Data);
    CHECK(exit_code_for(std::overflow_error("size")) == ExitCode::Data);
    CHECK(exit_code_for(std::runtime_error("cannot open")) == ExitCode::System);
    CHECK(exit_code_for(std::system_error(std::make_error_code(std::errc::no_space_on_device))) == ExitCode::System);
    CHECK(exit_code_for(std::ios_base::failure("io")) == ExitCode::System);
    CHECK(exit_code_for(std::logic_error("bug")) == ExitCode::Internal);
    CHECK(exit_code_for(std::bad_alloc()) == ExitCode::Internal);

    CHECK(static_cast<int>(ExitCode::Success) == 0);
    CHECK(static_cast<int>(ExitCode::Internal) == 1);
    CHECK(static_cast<int>(ExitCode::Usage) == 2);
    CHECK(static_cast<int>(ExitCode::System) == 3);
    CHECK(static_cast<int>(ExitCode::Data) == 4);
    CHECK(static_cast<int>(ExitCode::Limit) == 5);
}

TEST(real_failures_map_to_the_documented_exit_codes) {
    const auto dir = test::fresh_dir("exit_codes");
    const auto input = dir / "in.bin";
    const auto poem = dir / "in.poem";
    write_binary_file(input, pseudo_random(2000));
    encode_file(input, poem);

    const auto code_of = [](auto&& action) {
        try {
            action();
        }
        catch (const std::exception& error) {
            return exit_code_for(error);
        }
        return ExitCode::Success;
    };

    CHECK(code_of([&] { parse({ "encode", "x" }); }) == ExitCode::Usage);               // no -o
    CHECK(code_of([&] { parse({ "frobnicate" }); }) == ExitCode::Usage);
    CHECK(code_of([&] { parse({ "verify", "x", "--max-output", "abc" }); }) == ExitCode::Usage);

    CHECK(code_of([&] { verify_file(dir / "missing.poem"); }) == ExitCode::System);
    CHECK(code_of([&] { encode_file(dir / "missing.bin", dir / "o.poem"); }) == ExitCode::System);
    CHECK(code_of([&] { encode_file(input, poem); }) == ExitCode::System);             // output exists
    CHECK(code_of([&] { encode_file(input, input, CompressionMode::Auto, 0, true); }) == ExitCode::System);

    write_binary_file(dir / "junk.poem", pseudo_random(100));
    CHECK(code_of([&] { verify_file(dir / "junk.poem"); }) == ExitCode::Data);

    auto text = std::string();
    {
        const auto bytes = test::read_all(poem);
        text.assign(bytes.begin(), bytes.end());
    }
    text.resize(text.size() / 2);
    write_binary_file(dir / "cut.poem", { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
    CHECK(code_of([&] { verify_file(dir / "cut.poem"); }) == ExitCode::Data);

    CHECK(code_of([&] { encode_file(input, dir / "big.poem", CompressionMode::Auto, 10); }) == ExitCode::Limit);
    CHECK(code_of([&] { verify_file(poem, 10); }) == ExitCode::Limit);

    CHECK(code_of([&] { verify_file(poem); }) == ExitCode::Success);
}

TEST(quiet_and_verbose_are_parsed_for_every_command) {
    CHECK(parse({ "encode", "in", "-o", "out" }).verbosity == Verbosity::Normal);

    for (const std::vector<std::string> command : {
             std::vector<std::string>{ "encode", "in", "-o", "out" },
             { "decode", "in", "-o", "out" },
             { "verify", "in" },
             { "inspect", "in" },
             { "preview", "in" },
             { "estimate", "in" },
             { "identify", "in" },
             { "completion", "powershell" } })
    {
        for (const std::string quiet : { "-q", "--quiet" }) {
            auto args = command;
            args.push_back(quiet);
            CHECK(parse(args).verbosity == Verbosity::Quiet);
        }

        for (const std::string verbose : { "-v", "--verbose" }) {
            auto args = command;
            args.push_back(verbose);
            CHECK(parse(args).verbosity == Verbosity::Verbose);
        }

        auto both = command;
        both.push_back("-q");
        both.push_back("-v");
        CHECK_THROWS_WITH(parse(both), "cannot be used together");
    }

    // -V is the version, -v is verbose: they must not be mixed up.
    CHECK(parse({ "-V" }).command == Command::Version);
    CHECK(parse({ "verify", "in", "-v" }).command == Command::Verify);
}

TEST(standard_streams_are_accepted_as_dash) {
    CHECK(parse({ "encode", "-", "-o", "-" }).input == "-");
    CHECK(*parse({ "encode", "-", "-o", "-" }).output == "-");
    CHECK(parse({ "decode", "-", "-o", "out" }).input == "-");
    CHECK(parse({ "verify", "-" }).input == "-");
    CHECK(parse({ "estimate", "-" }).input == "-");
    CHECK_THROWS_WITH(parse({ "estimate", "-", "--quick" }), "--quick needs a file");
}

TEST(completion_command_and_script) {
    CHECK(parse({ "completion", "powershell" }).shell == "powershell");
    CHECK_THROWS_WITH(parse({ "completion", "bash" }), "Unknown shell");
    CHECK_THROWS(parse({ "completion" }));
    CHECK_THROWS_WITH(completion_script("zsh"), "Unknown shell");

    const auto script = completion_script("powershell");
    CHECK(script.find("Register-ArgumentCompleter") != std::string::npos);

    for (const auto& spec : command_specs()) {
        CHECK(script.find("'" + std::string(spec.name) + "'") != std::string::npos);

        for (const auto& option : spec.options) {
            CHECK(script.find("'" + std::string(option.name) + "'") != std::string::npos);
        }
    }

    for (const auto& option : common_options()) {
        CHECK(script.find("'" + std::string(option.name) + "'") != std::string::npos);
    }

    CHECK(script.find("'none', 'zstd', 'auto'") != std::string::npos);
}

TEST(command_table_agrees_with_the_parser) {
    // The completion is generated from the table: every listed option must really be
    // accepted by the parser (with a value where it takes one), and every command must exist.
    for (const auto& spec : command_specs()) {
        std::vector<std::string> base{ std::string(spec.name), "x" };

        if (spec.name == "encode" || spec.name == "decode") {
            base.insert(base.end(), { "-o", "y" });
        }

        if (spec.name == "completion") {
            base[1] = "powershell";
        }

        parse(base);  // the command exists and its basic form is valid

        for (const auto& option : spec.options) {
            auto args = base;

            if (option.name == "-o" && (spec.name == "encode" || spec.name == "decode")) {
                continue;  // already part of the base line
            }

            args.push_back(std::string(option.name));

            if (option.takes_value) {
                args.push_back(option.values.empty() ? "3" : std::string(option.values.front()));
            }

            parse(args);  // must not throw
        }
    }

    // And an option that is not in the table is refused.
    CHECK_THROWS(parse({ "inspect", "x", "--force" }));
    CHECK_THROWS(parse({ "identify", "x", "--stanzas", "3" }));
}

TEST(help_lists_the_new_options_and_exit_codes) {
    const auto help = help_text();

    for (const std::string word : { "completion", "--quiet", "--verbose", "Exit codes", "Progress",
                                    "standard input", "5  limit exceeded", "4  bad data" }) {
        CHECK(help.find(word) != std::string::npos);
    }
}

TEST(progress_lines_are_formatted_for_people) {
    CHECK(format_progress_line("Reading", 0, 0, 0, 0) == "Reading  0 bytes");
    CHECK(format_progress_line("Receiving input", 3ull << 20, 0, 0, 5) == "Receiving input  3.0 MiB");

    // Percent, output, and no estimate before a second has passed or 1 % is done.
    CHECK(format_progress_line("Writing poem", 42, 100, 0, 0.5) == "Writing poem  42%");
    CHECK(format_progress_line("Writing poem", 42, 100, 1288490189, 0.5) == "Writing poem  42%  1.2 GiB written");
    CHECK(format_progress_line("Writing poem", 0, 100, 0, 10) == "Writing poem  0%");

    // 25 % in 10 s -> 30 s left.
    CHECK(format_progress_line("Writing poem", 25, 100, 0, 10) == "Writing poem  25%  ~30 s left");
    CHECK(format_progress_line("Writing poem", 50, 100, 0, 120) == "Writing poem  50%  ~2 min 0 s left");

    // Finished: no estimate; done beyond total is clamped to 100 %.
    CHECK(format_progress_line("Restoring file", 100, 100, 0, 10) == "Restoring file  100%");
    CHECK(format_progress_line("Restoring file", 150, 100, 0, 10) == "Restoring file  100%");

    CHECK(format_duration(0) == "0 s");
    CHECK(format_duration(59.4) == "59 s");
    CHECK(format_duration(61) == "1 min 1 s");
    CHECK(format_duration(3600) == "1 h 0 min");
    CHECK(format_duration(7384) == "2 h 3 min");
    CHECK(format_duration(-5) == "0 s");
}

TEST(console_progress_throttles_draws_one_line_and_erases_it) {
    std::ostringstream out;
    auto now = ConsoleProgress::Clock::time_point{};
    ConsoleProgress progress(out, std::chrono::milliseconds(100), [&] { return now; });

    progress.begin("Reading", 100);
    progress.advance(1, 0);                       // the first update is drawn at once
    const auto first = out.str();
    CHECK(first.starts_with("\rReading  1%"));

    now += std::chrono::milliseconds(50);
    progress.advance(2, 0);                       // too soon: skipped
    CHECK(out.str() == first);

    now += std::chrono::milliseconds(60);
    progress.advance(3, 0);                       // 110 ms after the last draw: drawn
    CHECK(out.str().size() > first.size());
    CHECK(out.str().find("3%") != std::string::npos);

    progress.end();                               // the line is erased with spaces and a return
    CHECK(out.str().back() == '\r');
    const auto before_second_end = out.str();
    progress.end();                               // nothing more to erase
    CHECK(out.str() == before_second_end);

    progress.advance(50, 0);                      // after end(): ignored
    CHECK(out.str() == before_second_end);

    // A new stage erases nothing visible and starts drawing again at once.
    progress.begin("Restoring file", 0);
    progress.advance(1024, 0);
    CHECK(out.str().find("Restoring file  1.0 KiB") != std::string::npos);
}

TEST(progress_stages_of_encode_decode_and_verify) {
    const auto dir = test::fresh_dir("progress_stages");

    for (const bool compressible : { false, true }) {
        const auto data = compressible ? repetitive(300000) : pseudo_random(300000);
        const auto input = dir / "in.bin";
        const auto poem = dir / "in.poem";
        const auto restored = dir / "restored.bin";

        std::filesystem::remove(input);
        std::filesystem::remove(poem);
        std::filesystem::remove(restored);
        write_binary_file(input, data);

        Recorder encoding;
        const auto report = encode_file(input, poem, CompressionMode::Auto, 0, false, &encoding);

        CHECK(names(encoding) == (std::vector<std::string>{ "Reading", "Writing poem" }));
        CHECK(encoding.open_stages == 0);
        CHECK(encoding.stages[0].total == data.size());
        CHECK(encoding.stages[0].last_done == data.size());
        CHECK(encoding.stages[1].total == report.payload_size);
        CHECK(encoding.stages[1].last_done == report.payload_size);
        CHECK(encoding.stages[1].last_produced <= report.poem_size);
        if (!compressible) {
            CHECK(encoding.stages[1].last_produced > 0);  // a big poem is written in blocks
        }
        CHECK(encoding.stages[0].monotonic && encoding.stages[1].monotonic);

        Recorder decoding;
        const auto decoded = decode_file(poem, restored, default_max_output, false, &decoding);

        CHECK(decoded.original_size == data.size());
        CHECK(decoded.method == report.method);
        CHECK(names(decoding) == (std::vector<std::string>{ "Reading poem", "Restoring file" }));
        CHECK(decoding.open_stages == 0);
        CHECK(decoding.stages[0].total == report.payload_size);
        CHECK(decoding.stages[0].last_done == report.payload_size);
        CHECK(decoding.stages[1].total == data.size());
        CHECK(decoding.stages[1].last_done == data.size());
        CHECK(decoding.stages[1].last_produced == data.size());

        Recorder verifying;
        verify_file(poem, default_max_output, &verifying);
        CHECK(names(verifying) == names(decoding));
        CHECK(verifying.open_stages == 0);

        Recorder analyzing;
        const auto analysis = analyze_file(input, CompressionMode::Auto, 3, &analyzing);
        CHECK(names(analyzing) == (std::vector<std::string>{ "Analyzing" }));
        CHECK(analyzing.stages[0].last_done == data.size());
        CHECK(analysis.poem_size == report.poem_size);
    }
}

TEST(progress_stages_are_closed_when_an_operation_fails) {
    const auto dir = test::fresh_dir("progress_failure");
    const auto input = dir / "in.bin";
    const auto poem = dir / "in.poem";
    write_binary_file(input, pseudo_random(100000));
    encode_file(input, poem, CompressionMode::None);

    auto bytes = test::read_all(poem);
    bytes.resize(bytes.size() / 2);
    write_binary_file(dir / "cut.poem", bytes);

    Recorder recorder;
    CHECK_THROWS(verify_file(dir / "cut.poem", default_max_output, &recorder));
    CHECK(recorder.open_stages == 0);
    CHECK(!recorder.stages.empty());
    CHECK(recorder.stages.back().ended);

    // The stage is not started at all when the limit stops the operation first.
    Recorder limited;
    CHECK_THROWS(verify_file(poem, 10, &limited));
    CHECK(limited.open_stages == 0);
}

TEST(standard_input_and_output_work_like_files) {
    const auto dir = test::fresh_dir("stdio");
    const auto original = dir / "original.bin";
    const auto expected = dir / "expected.poem";
    write_binary_file(original, repetitive(50000));
    encode_file(original, expected);

    const auto spool_files_before = spool_files_in_temp();

    // stdin -> file
    {
        const Redirect in(0, original, true);
        encode_file("-", dir / "from_stdin.poem");
    }
    CHECK(test::read_all(dir / "from_stdin.poem") == test::read_all(expected));

    // file -> stdout
    {
        const Redirect out(1, dir / "to_stdout.poem", false);
        encode_file(original, "-");
    }
    CHECK(test::read_all(dir / "to_stdout.poem") == test::read_all(expected));

    // stdin -> stdout, both at once (a pipeline), then back
    {
        const Redirect in(0, original, true);
        const Redirect out(1, dir / "piped.poem", false);
        encode_file("-", "-");
    }
    CHECK(test::read_all(dir / "piped.poem") == test::read_all(expected));

    {
        const Redirect in(0, dir / "piped.poem", true);
        const Redirect out(1, dir / "restored.bin", false);
        const auto report = decode_file("-", "-");
        CHECK(report.original_size == 50000);
    }
    CHECK(test::files_equal(original, dir / "restored.bin"));

    // verify and identify read a poem from stdin as well
    {
        const Redirect in(0, expected, true);
        CHECK(verify_file("-").original_size == 50000);
    }
    {
        const Redirect in(0, expected, true);
        CHECK(identify_file("-").original_size == 50000);
    }

    // analysis of standard input
    {
        const Redirect in(0, original, true);
        CHECK(analyze_file("-", CompressionMode::Auto, 0).poem_size == std::filesystem::file_size(expected));
    }

    // No work file stays behind.
    CHECK(spool_files_in_temp() == spool_files_before);
}

TEST(standard_input_respects_the_input_limit_and_cleans_up) {
    const auto dir = test::fresh_dir("stdio_limit");
    const auto original = dir / "big.bin";
    write_binary_file(original, pseudo_random(100000));

    const auto spool_files_before = spool_files_in_temp();

    {
        const Redirect in(0, original, true);
        CHECK_THROWS_WITH(encode_file("-", dir / "out.poem", CompressionMode::Auto, 1000), "Input limit exceeded");
    }

    CHECK(!std::filesystem::exists(dir / "out.poem"));
    CHECK(spool_files_in_temp() == spool_files_before);

    {
        const Redirect in(0, original, true);
        encode_file("-", dir / "out.poem", CompressionMode::Auto, 100000);  // exactly at the limit
    }
    CHECK(std::filesystem::exists(dir / "out.poem"));
}

TEST(a_damaged_poem_on_standard_input_is_rejected_cleanly) {
    const auto dir = test::fresh_dir("stdio_damage");
    write_binary_file(dir / "junk.poem", pseudo_random(5000));

    {
        const Redirect in(0, dir / "junk.poem", true);
        CHECK_THROWS(verify_file("-"));
    }

    {
        const Redirect in(0, dir / "junk.poem", true);
        CHECK_THROWS(decode_file("-", dir / "out.bin"));
    }

    CHECK(!std::filesystem::exists(dir / "out.bin"));
    CHECK(test::count_entries(dir) == 1);
}

TEST(output_dash_never_protects_or_conflicts_with_files) {
    // "-" is standard output: it must not be treated as a file name by the overwrite check.
    const auto dir = test::fresh_dir("stdio_dash");
    const auto original = dir / "a.bin";
    write_binary_file(original, repetitive(1000));

    {
        const Redirect out(1, dir / "out.poem", false);
        encode_file(original, "-", CompressionMode::Auto, default_max_input, false);
    }

    CHECK(std::filesystem::exists(dir / "out.poem"));
    CHECK(!std::filesystem::exists(dir / "-"));
    CHECK(is_stdio("-"));
    CHECK(!is_stdio("a.bin"));
    CHECK(!is_stdio(".\\-"));
}
