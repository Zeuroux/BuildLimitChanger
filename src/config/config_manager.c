#include "config_manager.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

enum {
    CONFIG_NAME_MAX = 129,
    CONFIG_SECTION_MAX = 64,
    CONFIG_PATH_MAX = 4096
};

typedef struct ConfigSection {
    char name[CONFIG_NAME_MAX];
    size_t name_len;
    int16_t min;
    int16_t max;
} ConfigSection;

typedef struct ConfigManager {
    char folder_path[CONFIG_PATH_MAX];
    int hook_version;
    ConfigSection sections[CONFIG_SECTION_MAX];
    size_t section_count;
    int64_t config_mtime;
    bool loaded;
} ConfigManager;

static ConfigManager g_config;

static const char k_filename[] = "config.ini";
static const char k_header[] =
    "# Config Instructions\n"
    "# --------------------------\n"
    "# hook_version:\n"
    "#   Optional override (0, 1, or 2).\n"
    "#   Leave unset unless you understand the implications.\n"
    "#\n"
    "# min:\n"
    "#   Minimum build height.\n"
    "#   Must be divisible by 16; otherwise, it will be adjusted to the nearest valid value.\n"
    "#   Must not go below -2048; otherwise, it will be set to -2048.\n"
    "#   Modifying this may affect world generation and can cause corruption or crashes.\n"
    "#   Ignored on versions 1.16.40 and below.\n"
    "#\n"
    "# max:\n"
    "#   Maximum build height.\n"
    "#   Must be divisible by 16; otherwise, it will be adjusted to the nearest valid value.\n"
    "#   Must not go above 2048; otherwise, it will be set to 2048.\n"
    "#   Setting this below the default may affect world generation or cause crashes.\n"
    "#   Cannot exceed 256 on versions 1.16.40 and below.\n"
    "#   Note: In Minecraft 1.16.40, there is a vanilla game bug where building above\n"
    "#   height 256 causes a crash, even if the configured max is 256.\n"
    "#   This is a Minecraft issue, not caused by this mod.\n"
    "\n"
    "# Extra important notes\n"
    "# --------------------------\n"
    "# This does not work on realms\n"
    "# This does not work on servers, unless the server has this mod installed\n"
    "# This does not work on multiplayer, unless the host and the other players has this mod installed\n"
    "# Server and client must have the same build limit, if they dont it might cause weird glitches\n"
    "# For mobs to spawn above the nether the spawning platform must have a roof because the nether mobs are \"cave spawns\"\n"
    "\n";

#if defined(_WIN32)
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

static bool has_trailing_separator(const char *path)
{
    size_t len;

    if (!path || path[0] == '\0')
        return false;

    len = strlen(path);
    return path[len - 1] == '/' || path[len - 1] == '\\';
}

static bool join_path(char *out, size_t out_size, const char *base, const char *name)
{
    size_t base_len = base ? strlen(base) : 0;
    size_t name_len = name ? strlen(name) : 0;
    bool needs_separator = base_len > 0 && !has_trailing_separator(base);
    char *write;

    if (!out || out_size == 0 ||
        base_len + (needs_separator ? 1u : 0u) + name_len >= out_size)
        return false;

    write = out;
    if (base_len > 0) {
        memcpy(write, base, base_len);
        write += base_len;
    }
    if (needs_separator)
        *write++ = PATH_SEPARATOR;
    if (name_len > 0) {
        memcpy(write, name, name_len);
        write += name_len;
    }
    *write = '\0';

    return true;
}

#if defined(_WIN32)
static bool utf8_to_wide(const char *value, wchar_t *out, size_t out_count)
{
    int len;
    int written;

    if (!value || !out || out_count == 0)
        return false;

    len = (int)strlen(value);
    written = MultiByteToWideChar(CP_UTF8, 0, value, len, out, (int)out_count - 1);
    if (written <= 0)
        return false;

    out[written] = L'\0';
    return true;
}

static bool create_directory_if_missing(const char *path)
{
    wchar_t wide[4096];

    if (!path || path[0] == '\0')
        return false;
    if (!utf8_to_wide(path, wide, sizeof(wide) / sizeof(wide[0])))
        return false;

    return CreateDirectoryW(wide, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int64_t get_write_time(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    wchar_t wide[4096];

    if (!path || !utf8_to_wide(path, wide, sizeof(wide) / sizeof(wide[0])) ||
        !GetFileAttributesExW(wide, GetFileExInfoStandard, &data))
        return -1;

    return ((int64_t)data.ftLastWriteTime.dwHighDateTime << 32) |
           (int64_t)data.ftLastWriteTime.dwLowDateTime;
}
#else
static bool create_directory_if_missing(const char *path)
{
    if (!path || path[0] == '\0')
        return false;
    if (mkdir(path, 0755) == 0)
        return true;
    return errno == EEXIST;
}

static int64_t get_write_time(const char *path)
{
    struct stat st;

    if (!path || stat(path, &st) != 0)
        return -1;

    return (int64_t)st.st_mtime;
}
#endif

static const char *ltrim(const char *p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    return p;
}

static void rtrim(char *p)
{
    char *end = p + strlen(p);

    while (end > p &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
}

static void strip_comment(char *p)
{
    for (; *p; ++p) {
        if (*p == '#') {
            *p = '\0';
            return;
        }
    }
}

static const char *clean_line(char *line)
{
    strip_comment(line);
    rtrim(line);
    return ltrim(line);
}

static bool parse_int16(const char *p, int16_t *out)
{
    char *end;
    long value;

    if (!p || !out)
        return false;

    errno = 0;
    p = ltrim(p);
    value = strtol(p, &end, 10);
    if (end == p || errno == ERANGE || value < INT16_MIN || value > INT16_MAX)
        return false;

    *out = (int16_t)value;
    return true;
}

static int16_t snap_to_16(int16_t value)
{
    int v = value;
    return (int16_t)((v + (v < 0 ? -8 : 8)) / 16 * 16);
}

static int16_t clamp_i16(int16_t value, int16_t min_value, int16_t max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static int16_t clean_height(int16_t value)
{
    return snap_to_16(clamp_i16(value, -2048, 2048));
}

static ConfigSection *find_section(const char *name, size_t name_len)
{
    for (size_t i = 0; i < g_config.section_count; ++i) {
        ConfigSection *section = &g_config.sections[i];
        if (section->name_len == name_len && memcmp(section->name, name, name_len) == 0)
            return section;
    }

    return NULL;
}

static ConfigSection *append_section(const char *name, size_t name_len, int16_t min_value, int16_t max_value)
{
    ConfigSection *section;

    if (!name || name_len == 0 || name_len >= CONFIG_NAME_MAX ||
        g_config.section_count >= CONFIG_SECTION_MAX)
        return NULL;

    section = &g_config.sections[g_config.section_count++];
    memcpy(section->name, name, name_len);
    section->name[name_len] = '\0';
    section->name_len = name_len;
    section->min = min_value;
    section->max = max_value;
    return section;
}

static bool sanitise_sections(void)
{
    bool changed = false;

    for (size_t i = 0; i < g_config.section_count; ++i) {
        ConfigSection *section = &g_config.sections[i];
        int16_t snapped_min = clean_height(section->min);
        int16_t snapped_max = clean_height(section->max);

        if (snapped_min != section->min) {
            section->min = snapped_min;
            changed = true;
        }
        if (snapped_max != section->max) {
            section->max = snapped_max;
            changed = true;
        }
    }

    return changed;
}

static void apply_defaults(void)
{
    g_config.section_count = 0;
    g_config.hook_version = -1;
    append_section("Overworld", 9, -64, 320);
    append_section("Nether", 6, 0, 128);
    append_section("TheEnd", 6, 0, 256);
}

static bool config_file_path(char *out, size_t out_size)
{
    return join_path(out, out_size, g_config.folder_path[0] ? g_config.folder_path : ".", k_filename);
}

static void save_config(void)
{
    char path[CONFIG_PATH_MAX];
    FILE *file;

    if (!config_file_path(path, sizeof(path)))
        return;

    file = fopen(path, "w");
    if (!file)
        return;

    fputs(k_header, file);

    if (g_config.hook_version >= 0)
        fprintf(file, "hook_version=%d\n\n", g_config.hook_version);

    for (size_t i = 0; i < g_config.section_count; ++i) {
        ConfigSection *section = &g_config.sections[i];
        fputc('[', file);
        fwrite(section->name, 1, section->name_len, file);
        fprintf(file, "]\nmin=%d\nmax=%d\n\n", section->min, section->max);
    }

    fclose(file);
    g_config.config_mtime = get_write_time(path);
}

static void load_config(void)
{
    char path[CONFIG_PATH_MAX];
    FILE *file;
    char line[1024];
    ConfigSection *current = NULL;
    bool good = true;
    bool dirty;

    if (!config_file_path(path, sizeof(path)))
        return;

    file = fopen(path, "r");
    if (!file) {
        apply_defaults();
        g_config.loaded = true;
        save_config();
        return;
    }

    g_config.section_count = 0;
    g_config.hook_version = -1;

    while (good && fgets(line, sizeof(line), file)) {
        bool complete = strchr(line, '\n') != NULL || feof(file);
        const char *p;
        const char *eq;

        if (!complete) {
            good = false;
            break;
        }

        p = clean_line(line);
        if (*p == '\0')
            continue;

        if (*p == '[') {
            const char *close = strchr(p + 1, ']');
            size_t name_len;

            if (!close) {
                good = false;
                break;
            }

            name_len = (size_t)(close - (p + 1));
            if (name_len == 0) {
                good = false;
                break;
            }

            current = find_section(p + 1, name_len);
            if (!current)
                current = append_section(p + 1, name_len, 0, 0);
            if (!current) {
                good = false;
                break;
            }
            continue;
        }

        eq = strchr(p, '=');
        if (!eq) {
            good = false;
            break;
        }

        {
            char key[64];
            size_t key_len = (size_t)(eq - p);
            const char *value;

            if (key_len == 0 || key_len >= sizeof(key)) {
                good = false;
                break;
            }

            memcpy(key, p, key_len);
            key[key_len] = '\0';
            rtrim(key);
            value = ltrim(eq + 1);

            if (current) {
                int16_t parsed = 0;
                if (strcmp(key, "min") == 0) {
                    if (!parse_int16(value, &parsed)) {
                        good = false;
                        break;
                    }
                    current->min = parsed;
                } else if (strcmp(key, "max") == 0) {
                    if (!parse_int16(value, &parsed)) {
                        good = false;
                        break;
                    }
                    current->max = parsed;
                }
            } else if (strcmp(key, "hook_version") == 0) {
                char *end;
                long parsed;

                errno = 0;
                parsed = strtol(value, &end, 10);
                if (end == value || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) {
                    good = false;
                    break;
                }
                g_config.hook_version = (int)parsed;
            }
        }
    }

    fclose(file);

    dirty = sanitise_sections();
    if (g_config.hook_version < -1 || g_config.hook_version > 2) {
        g_config.hook_version = -1;
        dirty = true;
    }

    if (!good || g_config.section_count == 0) {
        apply_defaults();
        g_config.loaded = true;
        save_config();
    } else {
        g_config.loaded = true;
        g_config.config_mtime = get_write_time(path);
        if (dirty)
            save_config();
    }
}

static void reload_if_needed(void)
{
    char path[CONFIG_PATH_MAX];
    int64_t write_time;

    if (!g_config.loaded) {
        load_config();
        return;
    }

    if (!config_file_path(path, sizeof(path)))
        return;

    write_time = get_write_time(path);

    if (write_time != g_config.config_mtime)
        load_config();
}

void config_manager_init(const char *folder_path)
{
    if (!join_path(g_config.folder_path, sizeof(g_config.folder_path),
                   folder_path ? folder_path : ".", "BuildLimitChanger"))
        return;

    g_config.loaded = false;
    g_config.config_mtime = -1;

    create_directory_if_missing(g_config.folder_path);
    load_config();
}

DimensionHeightRange config_manager_get_dimension(const char *name, size_t name_len,
                                                  int16_t def_min, int16_t def_max)
{
    DimensionHeightRange result;
    ConfigSection *section;

    result.min = def_min;
    result.max = def_max;

    if (!name || name_len == 0)
        return result;

    reload_if_needed();

    section = find_section(name, name_len);
    if (section) {
        result.min = section->min;
        result.max = section->max;
        return result;
    }

    def_min = clean_height(def_min);
    def_max = clean_height(def_max);

    if (append_section(name, name_len, def_min, def_max))
        save_config();

    result.min = def_min;
    result.max = def_max;
    return result;
}

int config_manager_get_hook_version_override(void)
{
    reload_if_needed();
    return g_config.hook_version;
}
