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

}  // namespace bardzip
