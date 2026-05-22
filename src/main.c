#include "binary_parser/section_parser.h"
#include "config/config_manager.h"
#include "dimension_hook.h"
#include "utils.h"

#include <string.h>

#if defined(__ANDROID__)
#define MOD_API __attribute__((visibility("default")))
#elif defined(__linux__)
#define MOD_API __attribute__((constructor))
#else
#define MOD_API
#endif

static void init(void)
{
    SectionList sections = { 0 };
    int text_index = -1;
    int hook_version;
    void *string_pos = NULL;

    if (!get_sections_runtime(&sections) || sections.count == 0)
        return;

    hook_version = config_manager_get_hook_version_override();

    for (size_t i = 0; i < sections.count; ++i) {
        SectionInfo *section = &sections.items[i];

        if (strcmp(section->name, ".text") == 0) {
            text_index = (int)i;
            continue;
        }

        if (!strstr(section->name, "data"))
            continue;

        if (hook_version < 0) {
            if (section_find_string(section, "1.21.120"))
                hook_version = 2;
            else if (section_find_string(section, "1.16.100"))
                hook_version = 1;
            else
                hook_version = 0;
        }

        if (!string_pos)
            string_pos = section_find_string(section, "A dimension task group");

        if (text_index >= 0 && hook_version >= 0 && string_pos)
            break;
    }

    if (hook_version >= 0 && text_index >= 0 && string_pos) {
        void *func = section_find_refs(&sections.items[text_index], string_pos);
        if (func)
            setup_hook(func, hook_version);
    }
}

MOD_API void mod_init(void)
{
#if defined(__ANDROID__)
    config_manager_init("/data/data/com.mojang.minecraftpe");
#elif defined(__linux__)
    config_manager_init(get_executable_dir());
#endif
    init();
}

#if defined(__ANDROID__)
__attribute__((constructor)) static void start(void)
{
    char path[1024];
    if (get_config_location(path, sizeof(path))) {
        config_manager_init(path);
        init();
    }
}
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>

static DWORD WINAPI start(LPVOID unused)
{
    (void)unused;
    config_manager_init(get_state_directory());
    init();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE thread;

        DisableThreadLibraryCalls(instance);
        thread = CreateThread(NULL, 0, start, NULL, 0, NULL);
        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
#endif
