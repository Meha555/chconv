#include "command/command.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "version.h"

namespace chconv {

const char *version_string()
{
    thread_local static char buf[64] = {0};
    std::snprintf(buf,
                  sizeof(buf),
                  "%s (libuchardet@%s, libiconv@%s, libmagic@%s)",
                  CHCONV_VERSION,
                  LIBCHARDET_VERSION,
                  LIBICONV_VERSION,
                  LIBMAGIC_VERSION);
    return buf;
}

cmdline::command create_root_command()
{
    cmdline::command command("chconv", "file encoding utility", [](cmdline::command *cmd) {
        std::cout << cmd->help();
        return EXIT_SUCCESS;
    });
    command.introduction("file encoding converter");
    command.version(version_string());
    command.add(create_guess_command());
    command.add(create_convert_command());
    return command;
}

} // namespace chconv
