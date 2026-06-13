#include "command/command.h"

#include <cstdlib>
#include <string>

#include "converter.h"

namespace chconv {
namespace {

convert_options_t parse_convert_options(cmdline::command *cmd)
{
    convert_options_t options;
    options.verbose = cmd->exist("verbose");
    options.recursive = cmd->exist("recursive");
    options.dry_run = cmd->exist("dry-run");
    options.force = cmd->exist("force");
    options.input = cmd->get<std::string>("input").value();
    options.output = cmd->get<std::string>("output").value();

    auto suffix_opt = cmd->get<std::string>("suffix");
    if (suffix_opt && !parse_regex_pairs(*suffix_opt, options.suffix)) {
        std::exit(EXIT_FAILURE);
    }

    auto exclude_opt = cmd->get<std::string>("exclude");
    if (exclude_opt && !parse_regex_pairs(*exclude_opt, options.exclude)) {
        std::exit(EXIT_FAILURE);
    }

    options.to = cmd->get<std::string>("to").value();
    return options;
}

int run_convert_command(cmdline::command *cmd)
{
    return run_convert(parse_convert_options(cmd));
}

} // namespace

cmdline::command create_convert_command()
{
    cmdline::command command("convert", "convert file encoding", run_convert_command);
    command.introduction("file encoding converter");
    // clang-format off
    command.flag("verbose", 'v', "print verbose output");
    command.flag("recursive", 'r', "process directories recursively");
    command.flag("dry-run", 'd', "just print files to be converted and do noting");
    command.flag("force", 'f', "overwrite if file already exists when output");
    command.option<std::string>("input", 'i', "input filename or directory", true);
    command.option<std::string>("output", 'o', "output filename or directory", true);
    command.option<std::string>("suffix", 's', cmdline::description("included file suffixes", "matched by regex or string list split by ';'"), false);
    command.option<std::string>("exclude", 'e', cmdline::description("excluded filenames, suffixes or dirs", "matched by regex or string list split by ';'"), false);
    command.option_with_default<std::string>("to", 't',
        cmdline::description(
            "encoding of output file",
            R"(see https://www.gnu.org/savannah-checkouts/gnu/libiconv/ for more information)"),
        false, "UTF-8");
    // clang-format on
    command.version(version_string());
    return command;
}

} // namespace chconv
