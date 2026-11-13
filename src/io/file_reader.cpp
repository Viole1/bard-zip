#include "io/file_reader.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>

namespace bardzip {

namespace {

template <typename Buffer>
Buffer read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);

    if (!input) {
        throw std::runtime_error("Failed to open input file");
    }

    const auto file_size = std::filesystem::file_size(path);

    if (file_size > max_input_file_size) {
        throw std::runtime_error("Input file exceeds maximum size of 64 MiB");
    }

    if (file_size > static_cast<std::uintmax_t>(
        (std::numeric_limits<std::size_t>::max)()))
    {
        throw std::overflow_error("Input file is too large");
    }

    const auto size = static_cast<std::size_t>(file_size);

    if (size > static_cast<std::size_t>(
        (std::numeric_limits<std::streamsize>::max)()))
    {
        throw std::overflow_error("Input file is too large to read");
    }

    Buffer buffer;
    buffer.resize(size);

    if (size != 0) {
        input.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(size)
        );

        if (!input || input.gcount() != static_cast<std::streamsize>(size)) {
            throw std::runtime_error("Failed to read input file completely");
        }
    }

    return buffer;
}

}

std::string read_text_file(const std::filesystem::path& path) {
    return read_file<std::string>(path);
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    return read_file<std::vector<std::uint8_t>>(path);
}

}
