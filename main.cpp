#include <condition_variable>
#include <cstdarg>
#include <cstring>
#include <errno.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>


#include "cmdline.h"
#include <iconv.h>
#include <uchardet.h>

#include "version.h"

namespace fs = std::filesystem;

namespace
{

// 并行任务处理器类
class ParallelProcessor
{
private:
    std::vector<std::thread> workers;
    std::queue<std::function<bool()>> tasks;
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    std::atomic<bool> has_error{false};
    const unsigned int num_threads = std::thread::hardware_concurrency() ? 0 : 2;

public:
    ParallelProcessor()
        : stop(false)
    {
        for (unsigned int i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<bool()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });

                        if (this->stop && this->tasks.empty()) {
                            return;
                        }

                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    if (!task()) {
                        this->has_error = true;
                    }
                }
            });
        }
    }

    ~ParallelProcessor()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    template<class F>
    void enqueue(F &&f)
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);

            // Don't allow enqueueing after stopping the pool
            if (stop) {
                throw std::runtime_error("enqueue on stopped ParallelProcessor");
            }

            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    bool has_failed() const
    {
        return has_error.load();
    }
};
struct guard_t
{
    guard_t()
    {
        cd = uchardet_new();
    }
    ~guard_t()
    {
        uchardet_delete(cd);
    }
    uchardet_t cd;
};
}

static const char *render_string(const char *fmt, ...)
{
    static char buf[64] = {0};
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

    guard_t guard;

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
                std::regex_search(filename, regex_pattern) // filename
            ) {
                return true;
            }

            if (!extension.empty()) {
                if (std::regex_search(extension, regex_pattern) || // extension with dot
                    std::regex_search(extension.substr(1), regex_pattern) // extension without dot
                ) {
                    return true;
                }
            }

            // Check if pattern matches any directory component
            for (const auto &part : path) {
                if (std::regex_search(part.string(), regex_pattern)) {
                    return true;
                }
            }
        } catch (const std::regex_error &ex) {
            // If regex_pattern is invalid, fall back to simple string matching
            if (path_str.find(pattern) != std::string::npos || // is directory
                filename.find(pattern) != std::string::npos || // is filename
                (!extension.empty() && extension == pattern)) { // is file's suffix
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

static bool should_include_suffix(const fs::path &path)
{
    // if no suffix specified, include all files
    if (!g.suffix) {
        return true;
    }

    const std::string extension = path.extension().string();

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
            if (std::regex_search(extension, regex_pattern) || // extension with dot
                std::regex_search(extension.substr(1), regex_pattern) // extension without dot
            ) {
                return true;
            }
        } catch (const std::regex_error &ex) {
            // If the regex_pattern is invalid, revert to simple string matching.
            if (extension == pattern || // full extension string with dot
                extension.substr(1) == pattern // full extension string without dot
            ) {
                return true;
            }
        }
    }

    return false;
}

// 线程安全的日志类
class ThreadSafeLogger
{
    friend void log(const char *fmt, ...);
    mutable std::mutex log_mutex;

public:
    // 模板函数，用于线程安全地打印日志
    template<typename... Args>
    void log(const char *format, Args... args) const
    {
        if (g.verbose) {
            std::lock_guard<std::mutex> lock(log_mutex);
            std::fprintf(stderr, format, args...);
        }
    }

    // 重载函数调用操作符，方便使用
    template<typename... Args>
    void operator()(const char *format, Args... args) const
    {
        log(format, args...);
    }

    // 线程安全地打印错误信息
    template<typename... Args>
    void error(const char *format, Args &&...args) const
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::fprintf(stderr, format, std::forward<Args>(args)...);
    }
};

// 全局日志实例
static const ThreadSafeLogger logger;

static void log(const char *fmt, ...)
{
    if (g.verbose) {
        std::lock_guard<std::mutex> lock(logger.log_mutex);
        va_list args;
        va_start(args, fmt);
        std::vfprintf(stderr, fmt, args);
        va_end(args);
    }
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
    uchardet_reset(g.guard.cd);
    uchardet_handle_data(g.guard.cd, buffer.data(), buffer.size());
    uchardet_data_end(g.guard.cd);

    const char *encoding = uchardet_get_charset(g.guard.cd);
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
    std::ifstream input_file(input_filename, std::ios::binary);
    if (!input_file.is_open()) {
        std::cerr << "cannot open file: " << input_filename << std::endl;
        return false;
    }

    input_file.seekg(0, std::ios::end);
    const std::streamsize size = input_file.tellg();

    input_file.seekg(0, std::ios::beg);
    std::vector<char> input_buffer(size);
    if (!input_file.read(input_buffer.data(), size)) {
        std::cerr << "failed to read: " << input_filename << std::endl;
        return false;
    }

    iconv_t cd = iconv_open(to_encoding.c_str(), from_encoding.c_str());
    if (cd == (iconv_t)-1) {
        std::cerr << "cannot convert " << input_filename << "(" << from_encoding << ") -> " << output_filename << "(" << to_encoding << "): " << std::strerror(errno) << "(" << errno << ")" << std::endl;
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
        std::cerr << "convert " << input_filename << "(" << from_encoding << ") -> " << output_filename << "(" << to_encoding << ") failed: " << std::strerror(errno) << "(" << errno << ")" << std::endl;
        iconv_close(cd);
        return false;
    }

    iconv_close(cd);

    std::ofstream output_file(output_filename, std::ios::binary);
    if (!output_file.is_open()) {
        std::cerr << "cannot open file: " << output_filename << std::endl;
        return false;
    }

    output_file.write(output_buffer.data(), output_buffer_size - out_left);
    return true;
}

static bool process_file(const fs::path &input_path, const fs::path &output_path)
{
    try {
        // detect file encoding
        const std::string file_encoding = detect_encoding(input_path);
        logger("%s: %s\n", input_path.string().c_str(), file_encoding.c_str());

        if (g.dry_run) {
            logger.error("would convert: %s(%s) -> %s(%s)\n",
                         input_path.string().c_str(), file_encoding.c_str(),
                         output_path.string().c_str(), g.to.value().c_str());
            return true;
        }

        fs::create_directories(fs::path(output_path).parent_path());

        if (convert_encoding(input_path, file_encoding, output_path, g.to.value())) {
            logger.error("converted: %s(%s) -> %s(%s)\n",
                         input_path.string().c_str(), file_encoding.c_str(),
                         output_path.string().c_str(), g.to.value().c_str());
            return true;
        }
        return false;
    } catch (const std::exception &ex) {
        logger.error("convert failed for %s: %s\n", input_path.string().c_str(), ex.what());
        return false;
    }
}

static bool process_directory(const fs::path &input_dir, const fs::path &output_dir)
{
    bool has_processed_file = false;
    std::vector<std::pair<fs::path, fs::path>> file_pairs; // <input_path, output_path>

    try {
        // 收集所有需要处理的文件
        if (g.recursive) {
            for (const auto &entry : fs::recursive_directory_iterator(input_dir)) {
                // Check if the entry should be excluded
                if (should_exclude(entry.path())) {
                    continue;
                }

                if (entry.is_regular_file()) {
                    const fs::path input_path = entry.path();
                    // use relative path to keep the directory structure
                    const fs::path relative_path = fs::relative(entry.path(), input_dir);

                    // Check suffix if specified
                    if (g.suffix) {
                        if (!should_include_suffix(input_path)) {
                            continue;
                        }
                    }

                    has_processed_file = true;
                    const fs::path output_path = fs::path(output_dir) / relative_path;
                    file_pairs.emplace_back(input_path, output_path);
                }
            }
        } else {
            for (const auto &entry : fs::directory_iterator(input_dir)) {
                // Check if the entry should be excluded
                if (should_exclude(entry.path())) {
                    continue;
                }

                if (entry.is_regular_file()) {
                    const fs::path input_path = entry.path();
                    const fs::path filename = entry.path().filename();

                    // Check suffix if specified
                    if (g.suffix) {
                        if (!should_include_suffix(input_path)) {
                            continue;
                        }
                    }

                    has_processed_file = true;
                    const fs::path output_path = fs::path(output_dir) / filename;
                    file_pairs.emplace_back(input_path, output_path);
                }
            }
        }

        if (!has_processed_file) {
            if (g.suffix) {
                logger.error("no file processed with suffix: \"%s\" in: %s\n", g.suffix.value().c_str(), input_dir.string().c_str());
            } else {
                logger.error("no file processed in: %s\n", input_dir.string().c_str());
            }
            return true;
        }

        ParallelProcessor processor;

        for (const auto &file_pair : file_pairs) {
            processor.enqueue([file_pair]() {
                return process_file(file_pair.first, file_pair.second);
            });
        }

        // 等待所有任务完成并检查是否有失败
        return !processor.has_failed();
    } catch (const std::exception &ex) {
        logger.error("directory processing failed: %s\n", ex.what());
        return false;
    }
}

int main(int argc, char *argv[])
{
    bool has_failed = false;
    try {
        g.init(argc, argv);
        logger.error("convert start...\n");

        g.input = fs::absolute(g.input);
        g.output = fs::absolute(g.output);

        if (!fs::exists(g.input)) {
            logger.error("input file or directory does not exist: %s\n", g.input.string().c_str());
            return 1;
        }

        // Check if input is directory
        if (fs::is_directory(g.input)) {
            if (!process_directory(g.input, g.output)) {
                has_failed = true;
            }
        }
        // Process single file
        else {
            // Check suffix if specified
            if (g.suffix) {
                if (!should_include_suffix(g.input)) {
                    logger.error("input file does not match any specified suffix\n");
                    return 1;
                }
            }

            if (!process_file(g.input, g.output)) {
                has_failed = true;
            }
        }

        if (has_failed) {
            logger.error("convert failed\n");
            return 1;
        }
        logger.error("convert done.\n");
        return 0;
    } catch (const std::exception &ex) {
        logger.error("convert failed: %s\n", ex.what());
        return 1;
    }
}