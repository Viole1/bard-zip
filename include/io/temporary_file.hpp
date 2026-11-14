#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace bardzip {

class TemporaryFile {
public:
    explicit TemporaryFile(const std::filesystem::path& destination);

    ~TemporaryFile() noexcept;

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    void write(std::span<const std::uint8_t> bytes);

    void commit();

private:
    std::filesystem::path temporary_path_;
    std::filesystem::path destination_path_;

    void* handle_ = nullptr;
    bool committed_ = false;
};

// A uniquely named scratch file next to `base` (base + ".work.<random>").
// The file is created empty on construction, opened by path by the caller
// (e.g. with std::ofstream / std::ifstream) and always deleted by the destructor.
class ScratchFile {
public:
    explicit ScratchFile(const std::filesystem::path& base);

    ~ScratchFile() noexcept;

    ScratchFile(const ScratchFile&) = delete;
    ScratchFile& operator=(const ScratchFile&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace bardzip
