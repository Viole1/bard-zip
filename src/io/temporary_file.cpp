#include "io/temporary_file.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

namespace bardzip {

namespace {

[[noreturn]] void throw_last_error(DWORD error, const char* message) {
    throw std::system_error(
        static_cast<int>(error),
        std::system_category(),
        message
    );
}

struct CreatedFile {
    std::filesystem::path path;
    HANDLE handle;
};

// Exclusively creates a new file named base + tag + <random>.
CreatedFile create_unique_file(
    const std::filesystem::path& base,
    const std::wstring& tag
) {
    if (base.empty() || base.filename().empty()) {
        throw std::invalid_argument(
            "Destination path must name a file"
        );
    }

    std::random_device random;

    constexpr int max_attempts = 100;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const std::uint64_t random_value =
            (static_cast<std::uint64_t>(random()) << 32) |
            static_cast<std::uint64_t>(random());

        auto candidate = base;
        candidate += tag + std::to_wstring(random_value);

        const HANDLE handle = CreateFileW(
            candidate.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (handle != INVALID_HANDLE_VALUE) {
            return { std::move(candidate), handle };
        }

        const DWORD error = GetLastError();

        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            throw_last_error(error, "Failed to create temporary file");
        }
    }

    throw std::runtime_error(
        "Failed to create temporary file after multiple attempts"
    );
}

}  // namespace

TemporaryFile::TemporaryFile(const std::filesystem::path& destination)
    : destination_path_(destination)
{
    auto created = create_unique_file(destination_path_, L".tmp.");

    temporary_path_ = std::move(created.path);
    handle_ = created.handle;
}

TemporaryFile::~TemporaryFile() noexcept {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }

    if (!committed_ && !temporary_path_.empty()) {
        DeleteFileW(temporary_path_.c_str());
    }
}

void TemporaryFile::write(std::span<const std::uint8_t> bytes) {
    if (committed_) {
        throw std::logic_error("Cannot write to a committed temporary file");
    }

    if (handle_ == nullptr) {
        throw std::logic_error("Temporary file handle is invalid");
    }

    constexpr std::size_t chunk_size = 64 * 1024;

    std::size_t offset = 0;

    while (offset < bytes.size()) {
        const auto bytes_to_write = static_cast<DWORD>(
            (std::min)(bytes.size() - offset, chunk_size)
        );

        DWORD bytes_written = 0;

        if (!WriteFile(
            handle_,
            bytes.data() + offset,
            bytes_to_write,
            &bytes_written,
            nullptr
        )) {
            throw_last_error(GetLastError(), "Failed to write temporary file");
        }

        if (bytes_written == 0) {
            throw std::runtime_error("WriteFile wrote zero bytes");
        }

        offset += bytes_written;
    }
}

void TemporaryFile::commit() {
    if (committed_) {
        throw std::logic_error("Temporary file has already been committed");
    }

    if (handle_ == nullptr) {
        throw std::logic_error("Temporary file handle is invalid");
    }

    if (!FlushFileBuffers(handle_)) {
        throw_last_error(GetLastError(), "Failed to flush temporary file");
    }

    const HANDLE handle = handle_;
    handle_ = nullptr;

    if (!CloseHandle(handle)) {
        throw_last_error(GetLastError(), "Failed to close temporary file");
    }

    if (!MoveFileExW(
        temporary_path_.c_str(),
        destination_path_.c_str(),
        MOVEFILE_WRITE_THROUGH
    )) {
        const DWORD error = GetLastError();

        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
            throw std::runtime_error("Output file already exists");
        }

        throw_last_error(error, "Failed to commit temporary file");
    }

    committed_ = true;
}

ScratchFile::ScratchFile(const std::filesystem::path& base) {
    auto created = create_unique_file(base, L".work.");
    path_ = std::move(created.path);

    if (!CloseHandle(created.handle)) {
        const DWORD error = GetLastError();
        DeleteFileW(path_.c_str());
        throw_last_error(error, "Failed to close scratch file");
    }
}

ScratchFile::~ScratchFile() noexcept {
    if (!path_.empty()) {
        DeleteFileW(path_.c_str());
    }
}

}
