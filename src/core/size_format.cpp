#include "core/size_format.hpp"

#include <iomanip>
#include <sstream>

namespace bardzip {

std::string format_size(std::uint64_t bytes) {
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;

    std::ostringstream text;
    text << std::fixed << std::setprecision(1);

    if (bytes >= gib) {
        text << static_cast<double>(bytes) / gib << " GiB";
    }
    else if (bytes >= mib) {
        text << static_cast<double>(bytes) / mib << " MiB";
    }
    else if (bytes >= kib) {
        text << static_cast<double>(bytes) / kib << " KiB";
    }
    else {
        text << bytes << " bytes";
    }

    return text.str();
}

std::string format_count(std::uint64_t value) {
    auto digits = std::to_string(value);

    for (auto position = static_cast<std::ptrdiff_t>(digits.size()) - 3; position > 0; position -= 3) {
        digits.insert(static_cast<std::size_t>(position), ",");
    }

    return digits;
}

}
