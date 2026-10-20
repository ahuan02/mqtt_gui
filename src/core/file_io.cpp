#include "core/file_io.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::string executable_directory() {
#ifdef _WIN32
    char buffer[MAX_PATH * 2] = {};
    const DWORD length = ::GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(sizeof buffer));
    if (length == 0 || length >= sizeof buffer) return ".";

    std::string path(buffer, length);
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
#else
    return ".";
#endif
}

std::string app_path(const char* filename) {
    std::string path = executable_directory();
    path += '\\';
    path += filename;
    return path;
}

std::string escape_field(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') continue;
        else out.push_back(ch);
    }
    return out;
}

std::string unescape_field(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[++i];
            out.push_back(next == 'n' ? '\n' : next);
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

bool read_whole_file(const std::string& path, std::string& content) {
#ifdef _WIN32
    HANDLE file = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    char buffer[8192];
    DWORD received = 0;
    while (::ReadFile(file, buffer, sizeof buffer, &received, nullptr) && received > 0)
        content.append(buffer, received);

    ::CloseHandle(file);
    return true;
#else
    (void)path;
    (void)content;
    return false;
#endif
}

bool write_whole_file(const std::string& path, const std::string& content) {
#ifdef _WIN32
    HANDLE file = ::CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
                                &written, nullptr);
    ::CloseHandle(file);
    return ok != 0 && written == content.size();
#else
    (void)path;
    (void)content;
    return false;
#endif
}
