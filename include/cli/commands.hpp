#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace bardzip {

// One command-line option of a command.
struct OptionSpec {
    std::string_view name;
    bool takes_value = false;
    std::vector<std::string_view> values;  // the values worth suggesting, if there is a fixed set
};

struct CommandSpec {
    std::string_view name;
    std::string_view summary;
    std::vector<OptionSpec> options;  // without the options every command accepts
};

// The commands and their options, as the shell completion needs them. The
// parser in arguments.cpp is the authority; a test checks that both agree.
const std::vector<CommandSpec>& command_specs();

// Options accepted by every command.
const std::vector<OptionSpec>& common_options();

// The names of the shells a completion script can be generated for.
const std::vector<std::string_view>& completion_shells();

// A script for the given shell ("powershell"); throws UsageError for others.
std::string completion_script(std::string_view shell);

}
