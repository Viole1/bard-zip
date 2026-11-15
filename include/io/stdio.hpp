#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <span>
#include <streambuf>
#include <vector>

namespace bardzip {

// "-" stands for standard input or output where a file name is expected
// (a file that is really called "-" can be given as ".\-").
bool is_stdio(const std::filesystem::path& path);

// Switches stdin and stdout to binary mode: otherwise Windows translates line
// breaks and treats Ctrl+Z as the end of the input. Idempotent.
void set_stdio_binary();

// Whether a standard stream (0 = in, 1 = out, 2 = error) is a console window
// rather than a file or a pipe.
bool is_console(int fd);

// A stream buffer over standard input.
class StdinStreambuf : public std::streambuf {
public:
    StdinStreambuf();

protected:
    int_type underflow() override;

private:
    std::vector<char> buffer_;
};

// The input of a file or of standard input, behind one interface.
class InputStream {
public:
    virtual ~InputStream() = default;
    virtual std::istream& stream() = 0;
};

std::unique_ptr<InputStream> open_input_stream(const std::filesystem::path& path);

// Writes to standard output (binary); throws std::runtime_error on failure,
// for example when a pipe was closed by the reader.
void write_stdout(std::span<const std::uint8_t> bytes);
void flush_stdout();

}
