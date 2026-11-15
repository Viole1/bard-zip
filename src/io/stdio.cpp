#include "io/stdio.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "io/file_reader.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <fcntl.h>
#include <io.h>

namespace bardzip {

namespace {

constexpr std::size_t stdin_buffer_size = 64 * 1024;

class FileInputStream final : public InputStream {
public:
    explicit FileInputStream(const std::filesystem::path& path)
        : file_(open_input_file(path))
    {
    }

    std::istream& stream() override { return file_; }

private:
    std::ifstream file_;
};

class StdinInputStream final : public InputStream {
public:
    StdinInputStream() : stream_(&buffer_) { set_stdio_binary(); }

    std::istream& stream() override { return stream_; }

private:
    StdinStreambuf buffer_;
    std::istream stream_;
};

}

bool is_stdio(const std::filesystem::path& path) {
    return path == std::filesystem::path("-");
}

void set_stdio_binary() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
}

bool is_console(int fd) {
    // Through the C runtime's descriptor table, so a redirected stream (also one
    // redirected inside the process) is reported correctly.
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    DWORD mode = 0;

    return handle != nullptr && handle != INVALID_HANDLE_VALUE &&
        GetConsoleMode(handle, &mode) != 0;
}

StdinStreambuf::StdinStreambuf() : buffer_(stdin_buffer_size) {}

StdinStreambuf::int_type StdinStreambuf::underflow() {
    if (gptr() != nullptr && gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }

    const auto count = std::fread(buffer_.data(), 1, buffer_.size(), stdin);

    if (count == 0) {
        if (std::ferror(stdin) != 0) {
            throw std::runtime_error("Failed to read standard input");
        }

        return traits_type::eof();
    }

    setg(buffer_.data(), buffer_.data(), buffer_.data() + count);
    return traits_type::to_int_type(*gptr());
}

std::unique_ptr<InputStream> open_input_stream(const std::filesystem::path& path) {
    if (is_stdio(path)) {
        return std::make_unique<StdinInputStream>();
    }

    return std::make_unique<FileInputStream>(path);
}

void write_stdout(std::span<const std::uint8_t> bytes) {
    set_stdio_binary();

    if (!bytes.empty() &&
        std::fwrite(bytes.data(), 1, bytes.size(), stdout) != bytes.size())
    {
        throw std::runtime_error("Failed to write to standard output");
    }
}

void flush_stdout() {
    if (std::fflush(stdout) != 0) {
        throw std::runtime_error("Failed to write to standard output");
    }
}

}
