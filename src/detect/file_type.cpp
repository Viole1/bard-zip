#include "detect/file_type.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace bardzip {

namespace {

struct Signature {
    std::size_t offset;
    std::string_view magic;
    FileType type;
};

using namespace std::string_view_literals;

// Order matters: longer and more specific signatures first.
constexpr std::array signatures{
    Signature{ 0, "\x89PNG\r\n\x1A\n"sv, { "PNG image", "png" } },
    Signature{ 0, "\xFF\xD8\xFF"sv, { "JPEG image", "jpg" } },
    Signature{ 0, "GIF87a"sv, { "GIF image", "gif" } },
    Signature{ 0, "GIF89a"sv, { "GIF image", "gif" } },
    Signature{ 0, "%PDF-"sv, { "PDF document", "pdf" } },
    Signature{ 0, "PK\x03\x04"sv, { "ZIP archive (also .docx, .xlsx, .jar, .apk ...)", "zip" } },
    Signature{ 0, "PK\x05\x06"sv, { "ZIP archive (empty)", "zip" } },
    Signature{ 0, "7z\xBC\xAF\x27\x1C"sv, { "7-Zip archive", "7z" } },
    Signature{ 0, "Rar!\x1A\x07"sv, { "RAR archive", "rar" } },
    Signature{ 0, "\x1F\x8B"sv, { "gzip compressed data", "gz" } },
    Signature{ 0, "BZh"sv, { "bzip2 compressed data", "bz2" } },
    Signature{ 0, "\xFD" "7zXZ\x00"sv, { "xz compressed data", "xz" } },
    Signature{ 0, "\x28\xB5\x2F\xFD"sv, { "Zstandard compressed data", "zst" } },
    Signature{ 0, "SQLite format 3\x00"sv, { "SQLite database", "sqlite" } },
    Signature{ 0, "\x7F" "ELF"sv, { "ELF executable (Linux)", "" } },
    Signature{ 0, "OggS"sv, { "Ogg media", "ogg" } },
    Signature{ 0, "fLaC"sv, { "FLAC audio", "flac" } },
    Signature{ 0, "ID3"sv, { "MP3 audio (with ID3 tag)", "mp3" } },
    Signature{ 0, "\x1A\x45\xDF\xA3"sv, { "Matroska / WebM video", "mkv" } },
    Signature{ 0, "II*\x00"sv, { "TIFF image", "tif" } },
    Signature{ 0, "MM\x00*"sv, { "TIFF image", "tif" } },
    Signature{ 0, "\x00\x00\x01\x00"sv, { "ICO icon", "ico" } },
    Signature{ 0, "{\\rtf"sv, { "RTF document", "rtf" } },
    Signature{ 0, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1"sv, { "Old Microsoft Office document (.doc, .xls ...)", "doc" } },
    Signature{ 4, "ftyp"sv, { "MP4 / MOV / HEIC media", "mp4" } },
    Signature{ 0, "MZ"sv, { "Windows executable (.exe / .dll)", "exe" } },
};

bool starts_with_signature(std::span<const std::uint8_t> head, const Signature& signature) {
    const auto end = signature.offset + signature.magic.size();

    return head.size() >= end &&
        std::memcmp(head.data() + signature.offset, signature.magic.data(),
                    signature.magic.size()) == 0;
}

// RIFF containers share the first four bytes; the format is named at offset 8.
bool riff_type(std::span<const std::uint8_t> head, std::string_view name) {
    return head.size() >= 12 &&
        std::memcmp(head.data(), "RIFF", 4) == 0 &&
        std::memcmp(head.data() + 8, name.data(), name.size()) == 0;
}

// BMP: "BM", four bytes of size, then two reserved 16-bit fields that are zero.
bool looks_like_bmp(std::span<const std::uint8_t> head) {
    return head.size() >= 10 && head[0] == 'B' && head[1] == 'M' &&
        head[6] == 0 && head[7] == 0 && head[8] == 0 && head[9] == 0;
}

// MP3 frames without an ID3 tag start with an 11-bit sync word.
bool looks_like_mp3_frame(std::span<const std::uint8_t> head) {
    return head.size() >= 2 && head[0] == 0xFF && (head[1] & 0xE0) == 0xE0 &&
        (head[1] & 0x06) != 0;
}

// Printable ASCII, tabs, line breaks and valid UTF-8; a multi-byte sequence
// cut off by the end of the sample is accepted.
bool looks_like_text(std::span<const std::uint8_t> head) {
    std::size_t i = 0;

    while (i < head.size()) {
        const auto byte = head[i];

        if (byte == '\t' || byte == '\n' || byte == '\r' || (byte >= 0x20 && byte < 0x7F)) {
            ++i;
            continue;
        }

        std::size_t length = 0;

        if (byte >= 0xC2 && byte <= 0xDF) {
            length = 2;
        }
        else if (byte >= 0xE0 && byte <= 0xEF) {
            length = 3;
        }
        else if (byte >= 0xF0 && byte <= 0xF4) {
            length = 4;
        }
        else {
            return false;
        }

        for (std::size_t k = 1; k < length; ++k) {
            if (i + k >= head.size()) {
                return true;  // cut by the end of the sample
            }

            if ((head[i + k] & 0xC0) != 0x80) {
                return false;
            }
        }

        i += length;
    }

    return true;
}

}

FileType detect_file_type(std::span<const std::uint8_t> head) {
    if (head.empty()) {
        return { "Empty file", "" };
    }

    for (const auto& signature : signatures) {
        if (starts_with_signature(head, signature)) {
            return signature.type;
        }
    }

    if (riff_type(head, "WEBP")) {
        return { "WebP image", "webp" };
    }

    if (riff_type(head, "WAVE")) {
        return { "WAV audio", "wav" };
    }

    if (riff_type(head, "AVI ")) {
        return { "AVI video", "avi" };
    }

    if (looks_like_bmp(head)) {
        return { "BMP image", "bmp" };
    }

    if (looks_like_mp3_frame(head)) {
        return { "MP3 audio", "mp3" };
    }

    if (head.size() >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF &&
        looks_like_text(head.subspan(3)))
    {
        return { "Plain text (UTF-8 with byte order mark)", "txt" };
    }

    if (looks_like_text(head)) {
        return { "Plain text (UTF-8)", "txt" };
    }

    return { "Unknown binary data", "" };
}

}
