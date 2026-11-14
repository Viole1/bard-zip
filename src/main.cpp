#include <exception>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "cli/arguments.hpp"
#include "core/file_operations.hpp"

namespace {

void print_report(const bardzip::EncodeReport& report) {
    std::cerr
        << "Compression method: " << bardzip::to_string(report.method) << '\n'
        << "Original bytes:     " << report.original_size << '\n'
        << "Payload bytes:      " << report.payload_size << '\n'
        << "Container bytes:    " << report.container_size << '\n'
        << "Poem bytes:         " << report.poem_size << '\n'
        << "Stanzas:            " << report.stanza_count << '\n';
}

}

int main(int argc, char* argv[]) {
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif

        const auto arguments = bardzip::parse_arguments(argc, argv);

        switch (arguments.command) {
        case bardzip::Command::Encode:
            print_report(bardzip::encode_file(
                arguments.input,
                *arguments.output,
                arguments.compression,
                arguments.max_input
            ));
            break;

        case bardzip::Command::Decode:
            bardzip::decode_file(
                arguments.input,
                *arguments.output,
                arguments.max_output
            );
            break;

        case bardzip::Command::Inspect:
            bardzip::inspect_file(arguments.input);
            break;

        case bardzip::Command::Verify:
            bardzip::verify_file(arguments.input, arguments.max_output);
            break;
        }

        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
