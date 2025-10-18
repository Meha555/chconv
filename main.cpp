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

static struct options
{
    bool verbose = false;
    bool dry_run = false;
    bool recursive = false;
    fs::path input;
    fs::path output;
    std::optional<std::string> suffix;
    std::optional<std::string> to;
    std::optional<std::string> exclude;

    void init(int argc, char *argv[])
    {
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
            suffix = parser.get<std::string>("suffix");
        }
        if (parser.exist("exclude")) {
            exclude = parser.get<std::string>("exclude");
        }
        // options with default value
        to = parser.get<std::string>("to");
    }

private:
    cmdline::parser parser;
} g;

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

static bool should_exclude(const fs::path &path)
{
    // if no exclude specified, do not exclude anything
    if (!g.exclude) {
        return false;
    }

    const std::string path_str = path.string();
    const std::string filename = path.filename().string();
    const std::string extension = path.extension().string();

    // Split exclude patterns by ';'
    const std::vector<std::string> patterns = split_string(g.exclude.value(), ';');

    for (const auto &pattern : patterns) {
        try {
            std::regex regex_pattern(pattern);

            if (std::regex_search(path_str, regex_pattern) || // directory
                std::regex_search(filename, regex_pattern) || // filename
                std::regex_search(extension, regex_pattern) // extension
            ) {
                return true;
            }

            // Check if pattern matches any directory component:
            // e.g.: /home/tom/build/file.txt, --exclude=build, then it should be exclude
            for (const auto &part : path) {
                if (std::regex_search(part.string(), regex_pattern)) {
                    return true;
                }
            }
        } catch (const std::regex_error &ex) {
            // If regex_pattern is invalid, fall back to simple string matching
            if (path_str.find(pattern) != std::string::npos || // is directory
                filename.find(pattern) != std::string::npos || // is filename
                extension == pattern) { // is file's suffix
                return true;
            }

            // Check if pattern is a directory name and matches a parent directory
            if (fs::is_directory(path)) {
                for (const auto &part : path) {
                    if (part.string() == pattern) {
                        return true;
                    }
                }
            }
        }
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

    // Split suffix patterns by ';'
    const std::vector<std::string> patterns = split_string(g.suffix.value(), ';');

    for (const auto &pattern : patterns) {
        try {
            std::regex regex_pattern(pattern);

            // Check if the file extension matches the regex_pattern
            if (std::regex_search(extension, regex_pattern)) {
                return true;
            }
        } catch (const std::regex_error &ex) {
            // If the regex_pattern is invalid, revert to simple string matching.
            if (extension == pattern) {
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
        if (g.recursive) {
            for (const auto &entry : fs::recursive_directory_iterator(input_dir)) {
                // Check if the entry should be excluded
                if (should_exclude(entry.path())) {
                    continue;
                }
                // we treat regular file as processing unit
                if (entry.is_regular_file()) {
                    // use relative path to keep the directory structure
                    const fs::path relative_path = fs::relative(entry.path(), input_dir);

                    has_processed_file = true;
                    futures.emplace_back(std::async(std::launch::async, process_file, entry.path(), fs::path(output_dir) / relative_path)); // this is an absolute path
                }
            }
        } else {
            for (const auto &entry : fs::directory_iterator(input_dir)) {
                // Check if the entry should be excluded
                if (should_exclude(entry.path())) {
                    continue;
                }
                // we treat regular file as processing unit
                if (entry.is_regular_file()) {
                    const fs::path filename = entry.path().filename();
                    const fs::path relative_path = fs::relative(entry.path(), input_dir);

                    has_processed_file = true;
                    futures.emplace_back(std::async(std::launch::async, process_file, entry.path(), fs::path(output_dir) / filename));
                }
            }
        }

        if (!has_processed_file) {
            if (g.suffix) {
                serr << "no file processed with suffix: \"" << g.suffix.value() << "\" in: " << input_dir << '\n';
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