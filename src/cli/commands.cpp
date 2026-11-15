#include "cli/commands.hpp"

#include <sstream>

#include "core/errors.hpp"

namespace bardzip {

namespace {

const std::vector<std::string_view> compression_values{ "none", "zstd", "auto" };

std::string quoted(std::string_view text) {
    return "'" + std::string(text) + "'";
}

std::string power_shell_array(const std::vector<std::string_view>& items) {
    std::string text = "@(";

    for (std::size_t i = 0; i < items.size(); ++i) {
        text += (i ? ", " : "") + quoted(items[i]);
    }

    return text + ")";
}

}

const std::vector<OptionSpec>& common_options() {
    static const std::vector<OptionSpec> options{
        { "-q", false, {} },
        { "--quiet", false, {} },
        { "-v", false, {} },
        { "--verbose", false, {} },
        { "-h", false, {} },
        { "--help", false, {} },
    };

    return options;
}

const std::vector<CommandSpec>& command_specs() {
    static const std::vector<CommandSpec> specs{
        { "encode", "Turn a file into a poem", {
            { "-o", true, {} },
            { "--compression", true, compression_values },
            { "--max-input", true, {} },
            { "--force", false, {} },
        } },
        { "decode", "Restore the file hidden in a poem", {
            { "-o", true, {} },
            { "--max-output", true, {} },
            { "--force", false, {} },
        } },
        { "verify", "Check a poem completely", {
            { "--max-output", true, {} },
        } },
        { "inspect", "Print the container header", {} },
        { "preview", "Show the first stanzas and the exact size", {
            { "--stanzas", true, {} },
            { "--compression", true, compression_values },
        } },
        { "estimate", "Tell how large the poem would be", {
            { "--compression", true, compression_values },
            { "--quick", false, {} },
            { "--max-input", true, {} },
            { "-o", true, {} },
        } },
        { "identify", "Guess the type of the hidden file", {} },
        { "completion", "Print a tab-completion script", {} },
    };

    return specs;
}

const std::vector<std::string_view>& completion_shells() {
    static const std::vector<std::string_view> shells{ "powershell" };
    return shells;
}

std::string completion_script(std::string_view shell) {
    if (shell != "powershell") {
        throw UsageError("Unknown shell: " + std::string(shell) + " (supported: powershell)");
    }

    std::ostringstream script;

    script
        << "# Tab completion for bardzip (PowerShell).\n"
        << "# Install for the current session:\n"
        << "#   bardzip completion powershell | Out-String | Invoke-Expression\n"
        << "# Install for every session: add that line to your profile ($PROFILE).\n"
        << "Register-ArgumentCompleter -Native -CommandName 'bardzip', 'bardzip.exe' -ScriptBlock {\n"
        << "    param($wordToComplete, $commandAst, $cursorPosition)\n\n"
        << "    $commands = [ordered]@{\n";

    for (const auto& spec : command_specs()) {
        script << "        " << quoted(spec.name) << " = " << quoted(spec.summary) << "\n";
    }

    std::vector<std::string_view> common;
    for (const auto& option : common_options()) {
        common.push_back(option.name);
    }

    script << "    }\n"
           << "    $commonOptions = " << power_shell_array(common) << "\n"
           << "    $options = @{\n";

    for (const auto& spec : command_specs()) {
        std::vector<std::string_view> names;
        for (const auto& option : spec.options) {
            names.push_back(option.name);
        }

        script << "        " << quoted(spec.name) << " = " << power_shell_array(names) << "\n";
    }

    // Options that take a value, and the fixed values worth suggesting.
    std::vector<std::string_view> value_options;
    std::ostringstream fixed_values;

    for (const auto& spec : command_specs()) {
        for (const auto& option : spec.options) {
            if (!option.takes_value) {
                continue;
            }

            bool seen = false;
            for (const auto name : value_options) {
                seen = seen || name == option.name;
            }

            if (!seen) {
                value_options.push_back(option.name);

                if (!option.values.empty()) {
                    fixed_values << "        " << quoted(option.name) << " = "
                                 << power_shell_array(option.values) << "\n";
                }
            }
        }
    }

    script
        << "    }\n"
        << "    $valueOptions = " << power_shell_array(value_options) << "\n"
        << "    $fixedValues = @{\n"
        << fixed_values.str()
        << "    }\n"
        << "    $shells = " << power_shell_array(completion_shells()) << "\n\n"
        << "    function New-Completion([string]$text, [string]$tooltip) {\n"
        << "        [System.Management.Automation.CompletionResult]::new($text, $text, 'ParameterValue', $tooltip)\n"
        << "    }\n\n"
        << "    # The words typed after the program name, without the one being completed.\n"
        << "    $words = @($commandAst.CommandElements | Select-Object -Skip 1 | ForEach-Object { $_.ToString() })\n"
        << "    if ($wordToComplete -ne '' -and $words.Count -gt 0) {\n"
        << "        $words = @($words | Select-Object -SkipLast 1)\n"
        << "    }\n\n"
        << "    $command = if ($words.Count -gt 0) { $words[0] } else { $null }\n"
        << "    $previous = if ($words.Count -gt 0) { $words[-1] } else { $null }\n\n"
        << "    if ($words.Count -eq 0) {\n"
        << "        foreach ($name in $commands.Keys) {\n"
        << "            if ($name -like \"$wordToComplete*\") { New-Completion $name $commands[$name] }\n"
        << "        }\n"
        << "        foreach ($name in '--help', '--version') {\n"
        << "            if ($name -like \"$wordToComplete*\") { New-Completion $name $name }\n"
        << "        }\n"
        << "        return\n"
        << "    }\n\n"
        << "    if ($command -eq 'completion' -and $words.Count -eq 1) {\n"
        << "        foreach ($shell in $shells) {\n"
        << "            if ($shell -like \"$wordToComplete*\") { New-Completion $shell \"Script for $shell\" }\n"
        << "        }\n"
        << "        return\n"
        << "    }\n\n"
        << "    # After an option that takes a value: its fixed values, otherwise nothing,\n"
        << "    # so PowerShell completes file names.\n"
        << "    if ($valueOptions -contains $previous) {\n"
        << "        if ($fixedValues.ContainsKey($previous)) {\n"
        << "            foreach ($value in $fixedValues[$previous]) {\n"
        << "                if ($value -like \"$wordToComplete*\") { New-Completion $value \"$previous $value\" }\n"
        << "            }\n"
        << "        }\n"
        << "        return\n"
        << "    }\n\n"
        << "    if ($wordToComplete.StartsWith('-') -and $options.ContainsKey($command)) {\n"
        << "        foreach ($name in @($options[$command]) + $commonOptions) {\n"
        << "            if ($name -like \"$wordToComplete*\") { New-Completion $name $name }\n"
        << "        }\n"
        << "    }\n"
        << "}\n";

    return script.str();
}

}
