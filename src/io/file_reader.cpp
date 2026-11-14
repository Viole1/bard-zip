#include "io/file_reader.hpp"

#include <stdexcept>

namespace bardzip {

std::ifstream open_input_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);

    if (!input) {
        throw std::runtime_error("Failed to open input file");
    }

    return input;
}

std::size_t read_block(std::istream& input, std::span<std::uint8_t> buffer) {
    input.read(
        reinterpret_cast<char*>(buffer.data()),
        static_cast<std::streamsize>(buffer.size())
    );

    if (input.bad() || (input.fail() && !input.eof())) {
        throw std::runtime_error("Failed to read input file");
    }

    return static_cast<std::size_t>(input.gcount());
}

}
