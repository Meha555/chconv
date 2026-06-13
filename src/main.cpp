#include <cstdlib>
#include <iostream>

#include "command/command.h"

int main(int argc, char *argv[])
{
    try {
        cmdline::g_config.show_option_typename = false;
        auto root = chconv::create_root_command();
        return root(argc, argv);
    } catch (const std::exception &ex) {
        std::cerr << "chconv failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
