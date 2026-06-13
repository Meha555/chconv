#include "command/command.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "encoding.h"

namespace chconv {
namespace {

namespace fs = std::filesystem;

fs::path get_single_path_argument(const cmdline::command *cmd, const std::string &command_name)
{
    const auto &args = cmd->rest();
    if (args.size() != 1) {
        throw std::runtime_error(command_name + " requires exactly one path argument");
    }
    return args.front();
}

int run_guess(cmdline::command *cmd)
{
    const fs::path input = get_single_path_argument(cmd, "guess");
    std::cout << detect_encoding(input) << '\n';
    return EXIT_SUCCESS;
}

} // namespace

cmdline::command create_guess_command()
{
    cmdline::command command("guess", "detect file encoding", run_guess);
    command.introduction("detect file encoding");
    command.version(version_string());
    return command;
}

} // namespace chconv
