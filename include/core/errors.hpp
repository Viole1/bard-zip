#pragma once

#include <stdexcept>

namespace bardzip {

// A wrong command line: unknown option, missing value, a request that makes no
// sense (writing binary data to a terminal). Exit code 2.
class UsageError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// A size limit (--max-input / --max-output) was exceeded. Exit code 5.
class LimitError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Everything else is classified by its standard type, see cli/exit_codes.hpp:
//   std::invalid_argument (not UsageError)  -> bad data: damaged or foreign file
//   std::runtime_error, std::system_error   -> file or system problem
//   anything else                           -> internal error

}
