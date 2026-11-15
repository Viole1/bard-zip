#include "cli/exit_codes.hpp"

#include <ios>
#include <new>
#include <stdexcept>
#include <system_error>

#include "core/errors.hpp"

namespace bardzip {

ExitCode exit_code_for(const std::exception& error) noexcept {
    // Most specific first: several of these derive from each other.
    if (dynamic_cast<const UsageError*>(&error) != nullptr) {
        return ExitCode::Usage;
    }

    if (dynamic_cast<const LimitError*>(&error) != nullptr) {
        return ExitCode::Limit;
    }

    if (dynamic_cast<const std::bad_alloc*>(&error) != nullptr) {
        return ExitCode::Internal;
    }

    // Damaged or foreign input is reported with these two types.
    if (dynamic_cast<const std::invalid_argument*>(&error) != nullptr ||
        dynamic_cast<const std::overflow_error*>(&error) != nullptr)
    {
        return ExitCode::Data;
    }

    if (dynamic_cast<const std::logic_error*>(&error) != nullptr) {
        return ExitCode::Internal;
    }

    // Files, disks, the operating system (std::system_error and std::ios_base::failure
    // derive from std::runtime_error).
    if (dynamic_cast<const std::runtime_error*>(&error) != nullptr) {
        return ExitCode::System;
    }

    return ExitCode::Internal;
}

std::string_view describe(ExitCode code) noexcept {
    switch (code) {
    case ExitCode::Success:
        return "success";
    case ExitCode::Internal:
        return "internal error";
    case ExitCode::Usage:
        return "wrong usage";
    case ExitCode::System:
        return "file or system error";
    case ExitCode::Data:
        return "bad data";
    case ExitCode::Limit:
        return "limit exceeded";
    }

    return "unknown";
}

}
