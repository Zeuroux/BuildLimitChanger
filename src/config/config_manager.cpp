#include "config_manager.hpp"

#include <limits>

#if defined(_WIN32)
    #define NOMINMAX
    #include <windows.h>
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <cerrno>
#endif

static const char* ltrim(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static void rtrim(char* p) {
    char* end = p + strlen(p);
    while (end > p && (end[-1] == ' '  || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
}

static void stripComment(char* p) {
    for (; *p; ++p)
        if (*p == '#') { *p = '\0'; return; }
}

static const char* cleanLine(char* line) {
    stripComment(line);
    rtrim(line);
    return ltrim(line);
}

static bool parseInt(const char* p, int16_t& out) {
    p = ltrim(p);
    char* end;
    const long v = strtol(p, &end, 10);
    if (end == p ||
        v < std::numeric_limits<int16_t>::min() ||
        v > std::numeric_limits<int16_t>::max()) {
        return false;
    }
    out = static_cast<int16_t>(v);
    return true;
}

static int16_t snapTo16(int16_t v) {
    const int vi = v;
    return static_cast<int16_t>((vi + (vi < 0 ? -8 : 8)) / 16 * 16);
}

static char pathSeparator() {
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

static bool hasTrailingSeparator(const std::string& path) {
    return !path.empty() && (path.back() == '/' || path.back() == '\\');
}

static std::string joinPath(std::string_view base, std::string_view name) {
    if (base.empty())
        return std::string(name);

    std::string result(base);
    if (!hasTrailingSeparator(result))
        result.push_back(pathSeparator());
    result.append(name.data(), name.size());
    return result;
}

#if defined(_WIN32)
static std::wstring utf8ToWide(std::string_view value)
{
    if (value.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return {};

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

static bool pathExists(const std::string& path)
{
    if (path.empty())
        return false;

    const std::wstring wide = utf8ToWide(path);
    if (wide.empty())
        return false;

    return GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool createDirectoryIfMissing(const std::string& path)
{
    if (path.empty())
        return false;
    if (pathExists(path))
        return true;

    const std::wstring wide = utf8ToWide(path);
    if (wide.empty())
        return false;

    return CreateDirectoryW(wide.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::int64_t getWriteTime(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    const std::wstring wide = utf8ToWide(path);
    if (wide.empty() || !GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &data))
        return -1;

    return (static_cast<std::int64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
           static_cast<std::int64_t>(data.ftLastWriteTime.dwLowDateTime);
}
#else
static bool pathExists(const std::string& path)
{
    struct stat st{};
    return !path.empty() && stat(path.c_str(), &st) == 0;
}

static bool createDirectoryIfMissing(const std::string& path)
{
    if (path.empty())
        return false;
    if (mkdir(path.c_str(), 0755) == 0)
        return true;
    return errno == EEXIST && pathExists(path);
}

static std::int64_t getWriteTime(const std::string& path)
{
    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
        return -1;

#if defined(__APPLE__)
    return (static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1000000000ll) +
           static_cast<std::int64_t>(st.st_mtimespec.tv_nsec);
#else
    return (static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1000000000ll) +
           static_cast<std::int64_t>(st.st_mtim.tv_nsec);
#endif
}
#endif

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::init(const std::string& folderPath) {
    m_folderPath = joinPath(folderPath, "BuildLimitChanger");
    createDirectoryIfMissing(m_folderPath);
    load();
}

const std::string& ConfigManager::getConfigFolderPath() const {
    return m_folderPath;
}

DimensionHeightRange ConfigManager::getDimension(std::string_view name, int16_t defMin, int16_t defMax)
{
    load();

    DimensionHeightRange result{};
    if (const Section* s = findSection(name)) {
        result.min = s->min;
        result.max = s->max;
        return result;
    }

    defMin = snapTo16(std::max(defMin, static_cast<int16_t>(-2048)));
    defMax = snapTo16(std::min(defMax, static_cast<int16_t>( 2048)));

    m_sections.push_back({ std::string(name), defMin, defMax });
    save();

    result.min = defMin;
    result.max = defMax;
    return result;
}

int ConfigManager::getHookVersionOverride() {
    load();
    return m_hookVersion;
}

std::string ConfigManager::configFilePath() const {
    return joinPath(m_folderPath, k_filename);
}

ConfigManager::Section* ConfigManager::findSection(std::string_view name) {
    for (auto& s : m_sections)
        if (s.name == name) return &s;
    return nullptr;
}

const ConfigManager::Section* ConfigManager::findSection(std::string_view name) const {
    for (const auto& s : m_sections)
        if (s.name == name) return &s;
    return nullptr;
}

bool ConfigManager::sanitiseSections() {
    bool changed = false;
    for (auto& s : m_sections) {
        const int16_t clampedMin = std::max(s.min, static_cast<int16_t>(-2048));
        const int16_t clampedMax = std::min(s.max, static_cast<int16_t>( 2048));
        const int16_t snappedMin = snapTo16(clampedMin);
        const int16_t snappedMax = snapTo16(clampedMax);
        if (snappedMin != s.min) { s.min = snappedMin; changed = true; }
        if (snappedMax != s.max) { s.max = snappedMax; changed = true; }
    }
    return changed;
}

#ifdef _WIN32
using ssize_t = std::intptr_t;

ssize_t getline(char** lineptr, size_t* n, FILE* stream) {
    if (!lineptr || !n || !stream) return -1;

    if (*lineptr == nullptr || *n == 0) {
        *n = 128;
        *lineptr = static_cast<char*>(std::malloc(*n));
        if (!*lineptr) return -1;
    }

    size_t pos = 0;

    for (;;) {
        int c = std::fgetc(stream);

        if (c == EOF) {
            if (pos == 0) return -1;
            break;
        }

        if (pos + 1 >= *n) {
            const size_t new_size = (*n) * 2;
            char* new_ptr = static_cast<char*>(std::realloc(*lineptr, new_size));
            if (!new_ptr) return -1;

            *lineptr = new_ptr;
            *n = new_size;
        }

        (*lineptr)[pos++] = static_cast<char>(c);

        if (c == '\n') break;
    }

    (*lineptr)[pos] = '\0';
    return static_cast<ssize_t>(pos);
}
#endif

void ConfigManager::load() {
    const std::string path = configFilePath();
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        applyDefaults();
        save();
        return;
    }

    m_hookVersion = -1;
    m_sections.clear();

    char*   line = nullptr;
    size_t  cap  = 0;
    ssize_t len;

    Section* cur  = nullptr;
    bool     good = true;

    while (good && (len = getline(&line, &cap, f)) != -1) {
        const char* p = cleanLine(line);

        if (*p == '\0') continue;

        if (*p == '[') {
            const char* close = strchr(p + 1, ']');
            if (!close) { good = false; break; }

            const std::string name(p + 1, close);
            if (name.empty()) { good = false; break; }

            cur = findSection(name);
            if (!cur) {
                m_sections.push_back({ name, 0, 0 });
                cur = &m_sections.back();
            }
            continue;
        }

        const char* eq = strchr(p, '=');
        if (!eq) { good = false; break; }

        char key[64]{};
        const auto keyLen = static_cast<size_t>(eq - p);
        if (keyLen == 0 || keyLen >= sizeof(key)) { good = false; break; }
        memcpy(key, p, keyLen);
        rtrim(key);

        const char* val = ltrim(eq + 1);

        if (cur) {
            int16_t n{};
            if (strcmp(key, "min") == 0) {
                if (!parseInt(val, n)) { good = false; break; }
                cur->min = n;
            } else if (strcmp(key, "max") == 0) {
                if (!parseInt(val, n)) { good = false; break; }
                cur->max = n;
            }
        } else {
            if (strcmp(key, "hook_version") == 0) {
                char* end;
                const long v = strtol(val, &end, 10);
                if (end == val ||
                    v < std::numeric_limits<int>::min() ||
                    v > std::numeric_limits<int>::max()) {
                    good = false;
                    break;
                }
                m_hookVersion = static_cast<int>(v);
            }
        }
    }

    free(line);
    fclose(f);

    bool dirty = sanitiseSections();
    if (m_hookVersion < -1 || m_hookVersion > 2) {
        m_hookVersion = -1;
        dirty = true;
    }

    if (!good || m_sections.empty()) {
        applyDefaults();
        save();
    } else if (dirty) {
        save();
    }
}

void ConfigManager::save() {
    const std::string path = configFilePath();
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        return;
    }

    fputs(k_header, f);

    if (m_hookVersion >= 0)
        fprintf(f, "hook_version=%d\n\n", m_hookVersion);

    for (const auto& s : m_sections)
        fprintf(f, "[%s]\nmin=%d\nmax=%d\n\n", s.name.c_str(), s.min, s.max);

    fclose(f);
}

void ConfigManager::applyDefaults() {
    m_hookVersion = -1;
    m_sections = {
        { "Overworld", -64, 320 },
        { "Nether",      0, 128 },
        { "TheEnd",      0, 256 },
    };
}
