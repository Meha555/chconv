#pragma once

#include "cmdline.h"

namespace chconv {

const char *version_string();
cmdline::command create_guess_command();
cmdline::command create_convert_command();
cmdline::command create_root_command();

} // namespace chconv
