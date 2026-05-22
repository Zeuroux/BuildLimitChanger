#include "utils.hpp"

#include <cstddef>

#if defined(_WIN32)
    #define NOMINMAX
    #include <windows.h>
#elif defined(__linux__)
    #include <limits.h>
    #include <unistd.h>
#endif

static std::string parent_path(std::string path)
{
    if (path.empty())
        return ".";

    const auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return path.substr(0, 1);

    return path.substr(0, pos);
}

#if defined(_WIN32)
static std::string wide_to_utf8(const wchar_t* value, int len)
{
    if (!value || len <= 0)
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, value, len, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, len, result.data(), size, nullptr, nullptr);
    return result;
}

static std::string current_directory_fallback()
{
    const DWORD size = GetCurrentDirectoryW(0, nullptr);
    if (size == 0)
        return ".";

    std::wstring buffer(static_cast<size_t>(size), L'\0');
    const DWORD written = GetCurrentDirectoryW(size, buffer.data());
    if (written == 0)
        return ".";

    return wide_to_utf8(buffer.c_str(), static_cast<int>(written));
}
#else
static std::string current_directory_fallback()
{
    return ".";
}
#endif

std::string getExecutableDir() {
#if defined(_WIN32)
    std::wstring exe(static_cast<size_t>(MAX_PATH), L'\0');
    DWORD len = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (len == 0)
        return current_directory_fallback();

    while (len == exe.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        exe.resize(exe.size() * 2);
        len = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (len == 0)
            return current_directory_fallback();
    }

    return parent_path(wide_to_utf8(exe.c_str(), static_cast<int>(len)));
#elif defined(__linux__)
    char buffer[PATH_MAX]{};
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0)
        return current_directory_fallback();

    buffer[len] = '\0';
    return parent_path(buffer);
#else
    #error Unsupported platform
#endif
}

#if defined(__ANDROID__)
bool getConfigLocation(char *outPath, size_t outSize) {
    static constexpr size_t kMaxId = 512;
    char appId[kMaxId] = {};

    FILE *f = fopen("/proc/self/cmdline", "r");
    if (!f) return false;
    size_t n = fread(appId, 1, kMaxId - 1, f);
    fclose(f);
    if (n == 0) return false;

    const char *bases[] = { "/sdcard/games", "/sdcard/Android/media" };
    for (const char *base : bases) {
        int written = snprintf(outPath, outSize, "%s/%s/", base, appId);
        if (written > 0 && (size_t)written < outSize && access(outPath, W_OK) == 0)
            return true;
    }

    return false;
}
#endif

#if defined(_WIN32)
#include <winrt/Windows.Storage.h>

std::string GetStateDirectory() {
    try {
        auto path = winrt::Windows::Storage::ApplicationData::Current().RoamingFolder().Path();
        return wide_to_utf8(path.c_str(), static_cast<int>(path.size()));
    } catch (...) {}
    return getExecutableDir();
}
#endif
