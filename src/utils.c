#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "utils.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <appmodel.h>
#include <wchar.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

#ifndef BLC_PATH_MAX
#define BLC_PATH_MAX 4096
#endif

static void parent_path_in_place(char *path)
{
    char *slash;
    char *backslash;
    char *last;

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    last = slash > backslash ? slash : backslash;

    if (!last) {
        strcpy(path, ".");
        return;
    }

    *(last == path ? last + 1 : last) = '\0';
}

#if defined(_WIN32)
static bool wide_to_utf8(const wchar_t *value, int value_len, char *out, size_t out_size)
{
    int written;

    if (!value || value_len <= 0 || !out || out_size == 0)
        return false;

    written = WideCharToMultiByte(CP_UTF8, 0, value, value_len, out, (int)out_size - 1, NULL, NULL);
    if (written <= 0)
        return false;

    out[written] = '\0';
    return true;
}

static bool append_wide_path(wchar_t *out, size_t out_count, const wchar_t *base, const wchar_t *name)
{
    wchar_t buffer[BLC_PATH_MAX];
    size_t base_len;
    int written;

    if (!out || out_count == 0 || out_count > BLC_PATH_MAX || !base || !name)
        return false;

    base_len = wcslen(base);
    written = _snwprintf(buffer, out_count, L"%ls%ls%ls",
                         base,
                         (base_len > 0 && base[base_len - 1] != L'\\' && base[base_len - 1] != L'/') ? L"\\" : L"",
                         name);
    if (written <= 0 || (size_t)written >= out_count)
        return false;

    wmemcpy(out, buffer, (size_t)written + 1);
    return true;
}

static bool get_current_directory_fallback(char *out, size_t out_size)
{
    wchar_t wide[BLC_PATH_MAX];
    DWORD len = GetCurrentDirectoryW((DWORD)(sizeof(wide) / sizeof(wide[0])), wide);

    if (len == 0 || len >= sizeof(wide) / sizeof(wide[0]))
        return false;

    return wide_to_utf8(wide, (int)len, out, out_size);
}
#endif

const char *get_executable_dir(void)
{
    static char path[BLC_PATH_MAX];

#if defined(_WIN32)
    wchar_t wide[BLC_PATH_MAX];
    DWORD len = GetModuleFileNameW(NULL, wide, (DWORD)(sizeof(wide) / sizeof(wide[0])));

    if (len == 0 || len >= sizeof(wide) / sizeof(wide[0]) ||
        !wide_to_utf8(wide, (int)len, path, sizeof(path))) {
        if (!get_current_directory_fallback(path, sizeof(path)))
            strcpy(path, ".");
        return path;
    }

    parent_path_in_place(path);
    return path;
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);

    if (len <= 0) {
        strcpy(path, ".");
        return path;
    }

    path[len] = '\0';
    parent_path_in_place(path);
    return path;
#else
#error Unsupported platform
#endif
}

#if defined(__ANDROID__)
bool get_config_location(char *out_path, size_t out_size)
{
    enum { k_max_id = 512 };
    char app_id[k_max_id];
    FILE *f;
    size_t n;
    const char *bases[] = { "/sdcard/games", "/sdcard/Android/media" };

    if (!out_path || out_size == 0)
        return false;

    memset(app_id, 0, sizeof(app_id));

    f = fopen("/proc/self/cmdline", "r");
    if (!f)
        return false;

    n = fread(app_id, 1, sizeof(app_id) - 1, f);
    fclose(f);
    if (n == 0)
        return false;

    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        int written = snprintf(out_path, out_size, "%s/%s/", bases[i], app_id);
        if (written > 0 && (size_t)written < out_size && access(out_path, W_OK) == 0)
            return true;
    }

    return false;
}
#endif

#if defined(_WIN32)
const char *get_state_directory(void)
{
    static char path[BLC_PATH_MAX];
    wchar_t local_app_data[BLC_PATH_MAX];
    wchar_t family[BLC_PATH_MAX];
    wchar_t roaming_state[BLC_PATH_MAX];
    DWORD env_len;
    UINT32 family_len = (UINT32)(sizeof(family) / sizeof(family[0]));
    LONG rc;

    env_len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data,
                                      (DWORD)(sizeof(local_app_data) / sizeof(local_app_data[0])));
    if (env_len == 0 || env_len >= sizeof(local_app_data) / sizeof(local_app_data[0]))
        return get_executable_dir();

    rc = GetCurrentPackageFamilyName(&family_len, family);
    if (rc != ERROR_SUCCESS || family_len == 0)
        return get_executable_dir();

    if (!append_wide_path(roaming_state, sizeof(roaming_state) / sizeof(roaming_state[0]),
                          local_app_data, L"Packages"))
        return get_executable_dir();

    if (!append_wide_path(roaming_state, sizeof(roaming_state) / sizeof(roaming_state[0]),
                          roaming_state, family))
        return get_executable_dir();

    if (!append_wide_path(roaming_state, sizeof(roaming_state) / sizeof(roaming_state[0]),
                          roaming_state, L"RoamingState"))
        return get_executable_dir();

    if (!wide_to_utf8(roaming_state, (int)wcslen(roaming_state), path, sizeof(path)))
        return get_executable_dir();

    return path;
}
#endif
