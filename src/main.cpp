#include "binary_parser/section_parser.hpp"
#include "dimension_hook.hpp"
#include "config/config_manager.hpp"
#include "utils.hpp"
#include <string>
#ifdef __ANDROID__
#define MOD_API extern "C" __attribute__((visibility("default")))
#elif defined(__linux__)
#define MOD_API __attribute__((constructor)) 
#else
#define MOD_API
#endif

void init() {
    auto sections = get_sections_runtime();
    if (sections.empty()) {
        return;
    }

    int textIndex = -1;
    int hookVersion = ConfigManager::getInstance().getHookVersionOverride();
    void* strPos = nullptr;

    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& section = sections[i];
        if (section.name == ".text") {
            textIndex = static_cast<int>(i);
            continue;
        }

        if (section.name.find("data") == std::string::npos) {
            continue;
        }

        if (hookVersion < 0) {
            if (section.find_string("1.21.120"))
                hookVersion = 2;
            else if (section.find_string("1.16.100"))
                hookVersion = 1;
            else
                hookVersion = 0;
        }

        if (!strPos) {
            strPos = section.find_string("A dimension task group");
        }

        if (textIndex >= 0 && hookVersion >= 0 && strPos) {
            break;
        }
    }

    if (hookVersion < 0 || textIndex < 0 || !strPos) {
        return;
    }

    if (auto func = sections[textIndex].find_refs(strPos)) {
        setup_hook(func, hookVersion);
    }
}
MOD_API void mod_init() { 
    #ifdef __ANDROID__
        ConfigManager::getInstance().init("/data/data/com.mojang.minecraftpe");
    #elif defined(__linux__)
        ConfigManager::getInstance().init(getExecutableDir());
    #endif
    init();
}

#ifdef __ANDROID__
#include <jni.h>
#include <dlfcn.h>
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env;
    vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (void* handle = dlopen("libmcpelauncher_mod.so", RTLD_NOW); !handle) {
        ConfigManager::getInstance().init(ResolveGameStoragePath(env));
        init();
    } else {
        dlclose(handle);
    }
    return JNI_VERSION_1_6;
}
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
static DWORD WINAPI start(LPVOID) {
    ConfigManager::getInstance().init(GetStateDirectory());
    init();
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        if (HANDLE thread = CreateThread(nullptr, 0, start, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
#endif
