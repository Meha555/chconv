#include "converter.h"

#include <cstring>
#include <errno.h>
#include <execution>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <syncstream>
#include <thread>

#include "encoding.h"
#include <iconv.h>

namespace chconv {
namespace {

#define EXT_CONVERTED ".converted"
#define EXT_BACKUP ".bak"

struct SkippedFiles {
    std::vector<std::pair<fs::path, fs::path>> files; // <src, dst>
    std::mutex mtx;

    void append(const fs::path &src, const fs::path &dst)
    {
        std::lock_guard lck(mtx);
        files.emplace_back(src, dst);
    }
};

SkippedFiles g_skipped_files;

enum class ProcessingStatus {
    skip,
    success,
    error,
};

std::vector<std::string> split_string(const std::string &str, char delimiter)
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

bool should_exclude(const fs::path &path, const convert_options_t &options)
{
    if (!options.exclude) {
        return false;
    }

    const std::string path_str = path.string();
    const std::string filename = path.filename().string();
    const std::string extension = path.extension().string();
    try {
        const fs::path relative_path = fs::relative(path, options.input);

        const auto &regex_patterns = options.exclude->second;
        for (const auto &regex_pattern : regex_patterns) {
            try {
                if (std::regex_match(path_str, regex_pattern) ||
                    std::regex_match(filename, regex_pattern) ||
                    std::regex_match(extension, regex_pattern)) {
                    return true;
                }

                for (const auto &part : relative_path) {
                    if (std::regex_match(part.string(), regex_pattern)) {
                        return true;
                    }
                }
            } catch (const std::regex_error &) {
                if (path_str.find(options.exclude->first) != std::string::npos ||
                    filename.find(options.exclude->first) != std::string::npos ||
                    extension == options.exclude->first) {
                    return true;
                }

                if (fs::is_directory(path)) {
                    try {
                        for (const auto &part : relative_path) {
                            if (part.string() == options.exclude->first) {
                                return true;
                            }
                        }
                    } catch (const fs::filesystem_error &) {
                        if (path_str.find(options.exclude->first) != std::string::npos) {
                            return true;
                        }
                    }
                }
            }
        }
    } catch (const fs::filesystem_error &) {
        return path_str.find(options.exclude->first) != std::string::npos;
    }

    return false;
}

bool should_include_suffix(const fs::path &filepath, const convert_options_t &options)
{
    if (!options.suffix) {
        return true;
    }

    const std::string extension = filepath.extension().string();
    if (extension.empty()) {
        return false;
    }

    const auto &regex_patterns = options.suffix->second;
    for (const auto &regex_pattern : regex_patterns) {
        try {
            if (std::regex_match(extension, regex_pattern)) {
                return true;
            }
        } catch (const std::regex_error &) {
            if (extension == options.suffix->first) {
                return true;
            }
        }
    }

    return false;
}

bool convert_encoding(const fs::path &input_filename,
                      const std::string &from_encoding,
                      fs::path output_filename,
                      const std::string &to_encoding,
                      const convert_options_t &options)
{
    std::osyncstream serr(std::cerr);

    // 如果源编码和目标编码相同，则无需转码
    if (from_encoding == to_encoding) {
        try {
            if (input_filename != output_filename) {
                // 如果输出位置已有同名文件，且策略是不要替换，则生成.converted文件
                if (fs::exists(output_filename) && !options.force) {
                    output_filename.concat(EXT_CONVERTED);
                    g_skipped_files.append(input_filename, output_filename);
                    fs::copy_file(input_filename, output_filename);
                } else {
                    fs::copy_file(input_filename, output_filename, fs::copy_options::overwrite_existing);
                }
            }
            return true;
        } catch (const fs::filesystem_error &ex) {
            serr << "copy " << input_filename << "(" << from_encoding << ") -> " << output_filename << "(" << to_encoding << ") failed: " << ex.what() << '\n';
            return false;
        }
    }

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
    input_file.close();

    iconv_t cd = iconv_open(to_encoding.c_str(), from_encoding.c_str());
    if (cd == reinterpret_cast<iconv_t>(-1)) {
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
    if (result == static_cast<size_t>(-1)) {
        serr << "convert " << input_filename << "(" << from_encoding << ") -> " << output_filename << "(" << to_encoding << ") failed: " << std::strerror(errno) << "(" << errno << ")\n";
        iconv_close(cd);
        return false;
    }

    iconv_close(cd);

    bool is_same_file = input_filename == output_filename;
    fs::path bak_input_filename = input_filename;
    bak_input_filename += EXT_BACKUP;

    if (is_same_file) {
        fs::rename(input_filename, bak_input_filename);
    } else if (fs::exists(output_filename) && !options.force) {
        output_filename.concat(EXT_CONVERTED);
        g_skipped_files.append(input_filename, output_filename);
    }

    std::ofstream output_file(output_filename, std::ios::binary);
    if (!output_file.is_open()) {
        serr << "cannot open file: " << output_filename << '\n';
        return false;
    }

    output_file.write(output_buffer.data(), output_buffer_size - out_left);

    if (is_same_file) {
        if (output_file.good()) {
            fs::remove(bak_input_filename);
        } else {
            serr << "write " << output_filename << " failed."
                 << " state: " << output_file.exceptions()
                 << " error:" << std::strerror(errno) << "(" << errno << ")\n";
            return false;
        }
    }
    return true;
}

ProcessingStatus process_file(const fs::path &input_path, const fs::path &output_path, processing_context_t &context)
{
    const auto &options = context.options;
    std::osyncstream sout(std::cout);
    std::osyncstream serr(std::cerr);
    try {
        if (!should_include_suffix(input_path, options)) {
            return ProcessingStatus::skip;
        }
        if (!is_text_file(input_path)) {
            return ProcessingStatus::skip;
        }

        const std::string file_encoding = detect_encoding(input_path);
        if (file_encoding == "empty file") {
            if (options.dry_run || options.verbose) {
                sout << "skip empty file: " << input_path << '\n';
            }
            return ProcessingStatus::skip;
        }

        if (options.dry_run) {
            sout << "would convert: " << input_path << "(" << file_encoding << ") -> " << output_path << "(" << options.to.value() << ")\n";
            return ProcessingStatus::success;
        }

        if (output_path.has_parent_path()) {
            fs::create_directories(output_path.parent_path());
        }

        if (options.verbose) {
            sout << "converting: " << input_path << "(" << file_encoding << ") -> " << output_path << "(" << options.to.value() << ")\n";
        }

        if (!convert_encoding(input_path, file_encoding, output_path, options.to.value(), options)) {
            return ProcessingStatus::error;
        }
        ++context.processed_files;
        return ProcessingStatus::success;
    } catch (const std::exception &ex) {
        serr << "convert failed for " << input_path << ": " << ex.what() << '\n';
        return ProcessingStatus::error;
    }
}

ProcessingStatus process_directory(const fs::path &input_dir, const fs::path &output_dir, processing_context_t &context)
{
    const auto &options = context.options;
    std::osyncstream serr(std::cerr);

    bool has_failed = false;
    std::vector<std::pair<fs::path, fs::path>> tasks;
    try {
        std::vector<fs::path> input_dirs{input_dir};
        // Manually iterate instead of using fs::recursive_directory_iterator, besause we want to using minimum-suffix matching for entries, like VSCode.
        // Use index-based loop to avoid iterator invalidation when adding elements to vector
        for (size_t i = 0; i < input_dirs.size(); ++i) {
            for (const auto &entry : fs::directory_iterator(input_dirs[i])) {
                if (entry.is_directory()) {
                    if (should_exclude(entry.path(), options)) {
                        continue;
                    }
                    input_dirs.push_back(entry.path());
                }

                if (entry.is_regular_file() && !should_exclude(entry.path(), options)) {
                    const fs::path relative_path = fs::relative(entry.path(), input_dirs[i]);
                    const fs::path rel_dir = fs::relative(input_dirs[i], options.input);
                    const fs::path target_path = fs::weakly_canonical(output_dir / rel_dir / relative_path);
                    tasks.emplace_back(entry.path(), target_path);
                }
            }
        }

        if (tasks.size() >= std::thread::hardware_concurrency()) {
            auto result = std::transform_reduce(
                std::execution::par_unseq,
                tasks.cbegin(),
                tasks.cend(),
                true,
                [](bool a, bool b) {
                    return a && b;
                },
                [&context](const auto &task) {
                    return process_file(task.first, task.second, context) != ProcessingStatus::error;
                });
            has_failed = !result;
        } else {
            for (const auto &[input, output] : tasks) {
                if (process_file(input, output, context) == ProcessingStatus::error) {
                    has_failed = true;
                }
            }
        }
        return has_failed ? ProcessingStatus::error : ProcessingStatus::success;
    } catch (const std::exception &ex) {
        serr << "directory processing failed: " << ex.what() << '\n';
        return ProcessingStatus::error;
    }
}

} // namespace

bool parse_regex_pairs(const std::string &pattern, std::optional<regex_pairs_t> &pairs)
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

int run_convert(const convert_options_t &options)
{
    bool has_failed = false;

    std::cout << "convert start...\n";

    auto runtime_options = options;
    runtime_options.input = fs::weakly_canonical(runtime_options.input);
    runtime_options.output = fs::weakly_canonical(runtime_options.output);
    processing_context_t runtime_context(runtime_options);

    if (!fs::exists(runtime_options.input)) {
        std::cerr << "input file or directory does not exist: " << runtime_options.input << '\n';
        return EXIT_FAILURE;
    }

    if (fs::is_directory(runtime_options.input)) {
        if (process_directory(runtime_options.input, runtime_options.output, runtime_context) == ProcessingStatus::error) {
            has_failed = true;
        }
    } else {
        if (process_file(runtime_options.input, runtime_options.output, runtime_context) == ProcessingStatus::error) {
            has_failed = true;
        }
    }

    if (!g_skipped_files.files.empty()) {
        std::cerr << "Skipped files:\n";
        for (const auto &[src, dst] : g_skipped_files.files) {
            std::cerr << "  " << src << " -> " << dst << '\n';
        }
    }

    if (has_failed) {
        std::cerr << "convert failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "convert done. processed " << runtime_context.processed_files << " files.\n";
    return EXIT_SUCCESS;
}

} // namespace chconv
