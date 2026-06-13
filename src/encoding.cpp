#include "encoding.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "incbin.h"
#include <magic.h>
#include <uchardet.h>

namespace chconv {

INCBIN(magic_database_buffer, "../../misc/magic.mgc"); // MAGIC_MGC_FILE

template<typename Type, typename Ctor, typename Dtor>
struct ResourceGuard {
    template<typename... Args>
    explicit ResourceGuard(Args &&...args)
        : resource(Ctor{}(std::forward<Args>(args)...))
    {
        static_assert(std::is_invocable_v<Ctor, Args...>);
    }

    ~ResourceGuard()
    {
        static_assert(std::is_invocable_v<Dtor, Type>);
        (Dtor{})(resource);
    }

    operator Type() const
    {
        return resource;
    }

    Type resource;
};

namespace {
struct UchardetCtor {
    uchardet_t operator()() const
    {
        return uchardet_new();
    }
};

struct UchardetDtor {
    void operator()(uchardet_t cd) const
    {
        uchardet_delete(cd);
    }
};

struct MagicCtor {
    magic_t operator()(int flags) const
    {
        magic_t cookie = magic_open(flags);
        if (cookie == nullptr) {
            throw std::runtime_error("failed to open magic cookie");
        }
#if EMBED_MAGIC_MGC_FILE
        static const char *magic_buffers[1] = {
            reinterpret_cast<const char *>(&gmagic_database_bufferData),
        };
        static size_t sizes[1] = {
            gmagic_database_bufferSize,
        };
        if (magic_load_buffers(cookie, reinterpret_cast<void **>(&magic_buffers), sizes, 1) != 0) {
#else
        if (magic_load(cookie, MAGIC_MGC_FILE) != 0) {
#endif
            throw std::runtime_error("failed to load magic database: " + std::string(magic_error(cookie)));
        }
        return cookie;
    }
};

struct MagicDtor {
    void operator()(magic_t cookie) const
    {
        magic_close(cookie);
    }
};
} // namespace

using UchardetGuard = ResourceGuard<uchardet_t, UchardetCtor, UchardetDtor>;
using MagicGuard = ResourceGuard<magic_t, MagicCtor, MagicDtor>;

bool is_text_file(const fs::path &filepath)
{
    thread_local static MagicGuard magic(MAGIC_MIME_TYPE);
    const char *mime_type = magic_file(magic, filepath.string().c_str());
    if (mime_type == nullptr) {
        throw std::runtime_error("failed to detect mime type: " + std::string(magic_error(magic)));
    }
    return std::string(mime_type).find("text") != std::string::npos;
}

std::string detect_encoding(const fs::path &filename)
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
    file.close();

    // NOTE uchardet_reset的调用会修改uchardet_get_charset返回的字符串地址，
    // 所以在使用uchardet_get_charset返回的临时地址时，不能调用uchardet_reset
    // 因此将uchardet_reset放到最前面调用
    thread_local static UchardetGuard cd;
    uchardet_reset(cd);
    uchardet_handle_data(cd, buffer.data(), buffer.size());
    uchardet_data_end(cd);

    const char *encoding = uchardet_get_charset(cd);
    if (std::strcmp(encoding, "") == 0) {
        throw std::runtime_error("unrecognized encoding of file: " + filename.string() + ", maybe it is too short to guess charset?");
    }
    return encoding;
}

} // namespace chconv
