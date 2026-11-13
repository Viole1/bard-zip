#include "io/file_writer.hpp"

#include "io/temporary_file.hpp"

namespace bardzip {

void write_text_file(
    const std::filesystem::path& destination,
    std::string_view text
) {
    write_binary_file(
        destination,
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(text.data()),
            text.size()
        )
    );
}

void write_binary_file(
    const std::filesystem::path& destination,
    std::span<const std::uint8_t> bytes
) {
    TemporaryFile temporary(destination);

    temporary.write(bytes);
    temporary.commit();
}

}
