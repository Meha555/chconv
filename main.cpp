#include <cstdarg>
#include <cstring>
#include <errno.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <syncstream>

#include "cmdline.h"
#include <iconv.h>
#include <uchardet.h>
#include "version.h"

namespace fs = std::filesystem;

struct uchardet_guard_t
{
    uchardet_guard_t()
        : cd(uchardet_new())
    {
    }
    ~uchardet_guard_t()
    {
        uchardet_delete(cd);
    }
    operator uchardet_t() const
    {
        return cd;
    }
    uchardet_t cd;
};

enum class processing_status {
    skip,
    success,
    error,
};

static const char *render_string(const char *fmt, ...)
{
    thread_local static char buf[64] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsprintf(buf, fmt, args);
    va_end(args);
    return buf;
}

static std::vector<std::string> split_string(const std::string &str, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    return tokens;
}

using regex_pairs = std::pair<std::string, std::vector<std::regex>>;
static bool parse_regex_pairs(const std::string &pattern, std::optional<regex_pairs> &pairs)
{
    const auto pattern_strings = split_string(pattern, ';');
    pairs = std::make_pair(pattern, std::vector<std::regex>());
    try {
        for (const auto &pattern_str : pattern_strings) {
            pairs->second.emplace_back(pattern_str);
        }
    } catch (const std::regex_error &ex) {
        std::cerr << ex.what() << '\n';
        return false;
    }
    return true;
}

static struct options
{
    bool verbose = false;
    bool dry_run = false;
    bool recursive = false;
    fs::path input;
    fs::path output;
    std::optional<regex_pairs> suffix;
    std::optional<std::string> to;
    std::optional<regex_pairs> exclude;

    void init(int argc, char *argv[])
    {
        cmdline::g_config.show_option_typename = false;
        // clang-format off
        parser.program_name("chconv");
        parser.introduction("file encoding converter");
        parser.flag("verbose", 'v', "print verbose output");
        parser.flag("recursive", 'r', "process directories recursively");
        parser.flag("dry-run", 'd', "just print files to be converted and do noting");
        parser.option<std::string>("input", 'i', "input filename or directory", true);
        parser.option<std::string>("output", 'o', "output filename or directory", true);
        parser.option<std::string>("suffix", 's', cmdline::description("included file suffixes", "matched by regex or string and split by ';'"), false);
        parser.option<std::string>("exclude", 'e', cmdline::description("excluded filenames, suffixes or dirs", "matched by regex or string and split by ';'"), false);
        parser.option_with_default<std::string>("to", 't',
            cmdline::description(
                "encoding of output file",
                R"(see https://www.gnu.org/savannah-checkouts/gnu/libiconv/ for more information)"),
            false, "UTF-8");
        parser.version(render_string("%s (libuchardet@%s, libiconv@%s)",
                                     CHCONV_VERSION,
                                     LIBCHARDET_VERSION,
                                     LIBICONV_VERSION));
        // clang-format on
        parser.parse_check(argc, argv);

        // flags
        verbose = parser.exist("verbose");
        recursive = parser.exist("recursive");
        dry_run = parser.exist("dry-run");
        // required options
        input = parser.get<std::string>("input");
        output = parser.get<std::string>("output");
        // optional options
        if (parser.exist("suffix")) {
            if (!parse_regex_pairs(parser.get<std::string>("suffix"), suffix)) {
                std::exit(1);
            }
        }
        if (parser.exist("exclude")) {
            if (!parse_regex_pairs(parser.get<std::string>("exclude"), exclude)) {
                std::exit(1);
            }
        }
        // options with default value
        to = parser.get<std::string>("to");
    }

private:
    cmdline::parser parser;
} g;

static bool should_exclude(const fs::path &path)
{
    // if no exclude specified, do not exclude anything
    if (!g.exclude) {
        return false;
    }

    const std::string path_str = path.string();
    const std::string filename = path.filename().string();
    const std::string extension = path.extension().string();
    try {
        const fs::path relative_path = fs::relative(path, g.input);

        const auto &regex_patterns = g.exclude->second;
        for (const auto &regex_pattern : regex_patterns) {
            try {
                if (std::regex_match(path_str, regex_pattern) || // directory
                    std::regex_match(filename, regex_pattern) || // filename
                    std::regex_match(extension, regex_pattern) // extension
                ) {
                    return true;
                }

                // Check if pattern matches any directory component:
                // e.g.: cwd is /home/tom/chconv
                // /home/tom/chconv/ build/chconv/file.txt, --exclude=chconv, then it should be exclude
                // /home/tom/chconv/ build/file.txt, --exclude=chconv, then it shouldn't be exclude
                for (const auto &part : relative_path) {
                    if (std::regex_match(part.string(), regex_pattern)) {
                        return true;
                    }
                }
            } catch (const std::regex_error &ex) {
                // If regex_pattern is invalid, fall back to simple string matching
                if (path_str.find(g.exclude->first) != std::string::npos || // is directory
                    filename.find(g.exclude->first) != std::string::npos || // is filename
                    extension == g.exclude->first) { // is file's suffix
                    return true;
                }

                // Check if pattern is a directory name and matches a parent directory
                if (fs::is_directory(path)) {
                    try {
                        for (const auto &part : relative_path) {
                            if (part.string() == g.exclude->first) {
                                return true;
                            }
                        }
                    } catch (const fs::filesystem_error &) {
                        // If we can't compute relative path, fall back to simple string matching
                        if (path_str.find(g.exclude->first) != std::string::npos) {
                            return true;
                        }
                    }
                }
            }
        }
    } catch (const fs::filesystem_error &ex) {
        // If we can't compute relative path, fall back to simple string matching
        return path_str.find(g.exclude->first) != std::string::npos;
    }

    return false;
}

static bool should_include_suffix(const fs::path &filepath)
{
    // if no suffix specified, include all files
    if (!g.suffix) {
        return true;
    }

    const std::string extension = filepath.extension().string();

    // if file has no extension but we specified --sufix, exclude it
    if (extension.empty()) {
        return false;
    }
    const auto &regex_patterns = g.suffix->second;
    for (const auto &regex_pattern : regex_patterns) {
        try {
            // Check if the file extension matches the regex_pattern
            if (std::regex_match(extension, regex_pattern)) {
                return true;
            }
        } catch (const std::regex_error &ex) {
            // If the regex_pattern is invalid, revert to simple string matching.
            if (extension == g.suffix->first) {
                return true;
            }
        }
    }

    return false;
}

static std::string detect_encoding(const fs::path &filename)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("cannot open file: " + filename.string());
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return "empty file";
    }
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        throw std::runtime_error("failed to read: " + filename.string());
    }

    // NOTE uchardet_reset的调用会修改uchardet_get_charset返回的字符串地址，
    // 所以在使用uchardet_get_charset返回的临时地址时，不能调用uchardet_reset
    // 因此将uchardet_reset放到最前面调用
    uchardet_guard_t cd;
    uchardet_handle_data(cd, buffer.data(), buffer.size());
    uchardet_data_end(cd);

    const char *encoding = uchardet_get_charset(cd);
    if (std::strcmp(encoding, "") == 0) {
        throw std::runtime_error("unrecognized encoding of file: " + filename.string());
    }
    return encoding;
}

static bool convert_encoding(const fs::path &input_filename,
                             const std::string &from_encoding,
                             const fs::path &output_filename,
                             const std::string &to_encoding)
{
    std::osyncstream serr(std::cerr);

    std::ifstream input_file(input_filename, std::ios::binary);
    if (!input_file.is_open()) {
        serr << "cannot open file: " << input_filename << '\n';
        return false;
    }

    input_file.seekg(0, std::ios::end);
    const std::streamsize size = input_file.tellg();

    input_file.seekg(0, std::ios::beg);
    std::vector<char> input_buffer(size);
    if (!input_file.read(input_buffer.data(), size)) {
        serr << "failed to read: " << input_filename << '\n';
        return false;
    }

    iconv_t cd = iconv_open(to_encoding.c_str(), from_encoding.c_str());
    if (cd == (iconv_t)-1) {
        serr << "cannot convert " << input_filename << "(" << from_encoding << ") -> " << output_filename << "(" << to_encoding << "): " << std::strerror(errno) << "(" << errno << ")\n";
        return false;
    }

    // NOTE 分配输出缓冲区 (通常比输入大一些，因为编码可能扩充)
    size_t output_buffer_size = input_buffer.size() << 1;
    std::vector<char> output_buffer(output_buffer_size);

    char *in_ptr = input_buffer.data();
    size_t in_left = input_buffer.size();
    char *out_ptr = output_buffer.data();
    size_t out_left = output_buffer_size;

    size_t result = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    if (result == (size_t)-1) {
        serr << "convert " << input_filename << "(" << from_encoding << ") -> " << output_filename << "(" << to_encoding << ") failed: " << std::strerror(errno) << "(" << errno << ")\n";
        iconv_close(cd);
        return false;
    }

    iconv_close(cd);

    std::ofstream output_file(output_filename, std::ios::binary);
    if (!output_file.is_open()) {
        serr << "cannot open file: " << output_filename << '\n';
        return false;
    }

    output_file.write(output_buffer.data(), output_buffer_size - out_left);
    return true;
}

static processing_status process_file(const fs::path &input_path, const fs::path &output_path)
{
    std::osyncstream sout(std::cout);
    std::osyncstream serr(std::cerr);
    try {
        // Check suffix if specified
        if (!should_include_suffix(g.input)) {
            return processing_status::skip;
        }

        // detect file encoding
        const std::string file_encoding = detect_encoding(input_path);
        if (file_encoding == "empty file") {
            if (g.dry_run || g.verbose) {
                sout << "skip empty file: " << input_path << '\n';
            }
            return processing_status::skip;
        }

        if (g.dry_run) {
            sout << "would convert: " << input_path << "(" << file_encoding << ") -> " << output_path << "(" << g.to.value() << ")\n";
            return processing_status::success;
        }

        fs::create_directories(fs::path(output_path).parent_path());

        if (g.verbose) {
            sout << "converting: " << input_path << "(" << file_encoding << ") -> " << output_path << "(" << g.to.value() << ")\n";
        }
        return convert_encoding(input_path, file_encoding, output_path, g.to.value()) ? processing_status::success : processing_status::error;
    } catch (const std::exception &ex) {
        serr << "convert failed for " << input_path << ": " << ex.what() << '\n';
        return processing_status::error;
    }
}

static processing_status process_directory(const fs::path &input_dir, const fs::path &output_dir)
{
    std::osyncstream serr(std::cerr);

    bool has_failed = false;
    bool has_processed_file = false;
    std::vector<std::future<processing_status>> futures;
    try {
        std::vector<fs::path> input_dirs{input_dir};
        // Manually iterate instead of using fs::recursive_directory_iterator, besause we want to using minimum-suffix matching for entries, like VSCode.
        // Use index-based loop to avoid iterator invalidation when adding elements to vector
        for (size_t i = 0; i < input_dirs.size(); ++i) {
            for (const auto &entry : fs::directory_iterator(input_dirs[i])) {
                // Check if the entry should be excluded
                if (entry.is_directory()) {
                    if (should_exclude(entry.path())) {
                        continue;
                    } else {
                        input_dirs.push_back(entry.path());
                    }
                }
                // we treat regular file as processing unit
                if (entry.is_regular_file() && !should_exclude(entry.path())) {
                    const fs::path filename = entry.path().filename();
                    const fs::path relative_path = fs::relative(entry.path(), input_dirs[i]);

                    has_processed_file = true;
                    // keep the relative directory structure
                    const fs::path rel_dir = fs::relative(input_dirs[i], g.input);
                    const fs::path target_path = output_dir / rel_dir / relative_path;
                    futures.emplace_back(std::async(std::launch::async, process_file, entry.path(), target_path));
                }
            }
        }

        if (!has_processed_file) {
            if (g.suffix) {
                serr << "no file processed with suffix: \"" << g.suffix->first << "\" in: " << input_dir << '\n';
            } else {
                serr << "no file processed in: " << input_dir << '\n';
            }
        }

        for (auto &future : futures) {
            if (future.get() == processing_status::error) {
                has_failed = true;
            }
        }
        return has_failed ? processing_status::error : processing_status::success;
    } catch (const std::exception &ex) {
        serr << "directory processing failed: " << ex.what() << '\n';
        return processing_status::error;
    }
}

int main(int argc, char *argv[])
{
    bool has_failed = false;
    try {
        g.init(argc, argv);
        std::cout << "convert start...\n";

        g.input = fs::absolute(g.input);
        g.output = fs::absolute(g.output);

        if (!fs::exists(g.input)) {
            std::cerr << "input file or directory does not exist: " << g.input << '\n';
            return 1;
        }

        // Check if input is directory
        if (fs::is_directory(g.input)) {
            if (process_directory(g.input, g.output) == processing_status::error) {
                has_failed = true;
            }
        } else {
            if (process_file(g.input, g.output) == processing_status::error) {
                has_failed = true;
            }
        }

        if (has_failed) {
            std::cerr << "convert failed\n";
            return 1;
        }
        std::cout << "convert done.\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "convert failed: " << ex.what() << '\n';
        return 1;
    }
}