#pragma once

#include <atomic>
#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace chconv {

namespace fs = std::filesystem;

using regex_pairs_t = std::pair<std::string, std::vector<std::regex>>;

struct convert_options_t {
    bool verbose = false;
    bool dry_run = false;
    bool recursive = false;
    bool force = false;
    fs::path input;
    fs::path output;
    std::optional<regex_pairs_t> suffix;
    std::optional<std::string> to;
    std::optional<regex_pairs_t> exclude;
};

struct processing_context_t {
    explicit processing_context_t(const convert_options_t &options) : options(options) {}

    const convert_options_t &options;
    std::atomic_uint64_t processed_files = 0;
};

bool parse_regex_pairs(const std::string &pattern, std::optional<regex_pairs_t> &pairs);
int run_convert(const convert_options_t &options);

} // namespace chconv
