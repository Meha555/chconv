#pragma once

#include <filesystem>
#include <string>

namespace chconv {

namespace fs = std::filesystem;

bool is_text_file(const fs::path &filepath);
std::string detect_encoding(const fs::path &filename);

} // namespace chconv
