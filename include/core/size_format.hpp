#pragma once

#include <cstdint>
#include <string>

namespace bardzip {

// "1.5 MiB", "2.0 GiB", "512 bytes".
std::string format_size(std::uint64_t bytes);

// "1,234,567" - used where exact numbers matter.
std::string format_count(std::uint64_t value);

}
