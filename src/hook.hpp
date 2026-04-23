#pragma once
#if defined(_WIN32)
    #include <MinHook.h>
    struct HookInline {
        void* target   = nullptr;
        void* original = nullptr;

        template<typename Ret = void, typename... Args>
        Ret call(Args... args) const {
            return reinterpret_cast<Ret(__cdecl*)(Args...)>(original)(args...);
        }

        explicit operator bool() const noexcept { return original != nullptr; }
    };

    namespace hooker {
        inline MH_STATUS ensure_init() {
            static MH_STATUS s = MH_Initialize();
            return s;
        }

        inline HookInline create_inline(void* target, void* detour) {
            ensure_init();

            HookInline hook;
            hook.target = target;

            if (MH_CreateHook(target, detour, &hook.original) != MH_OK)
                return {};

            if (MH_EnableHook(target) != MH_OK) {
                MH_RemoveHook(target);
                return {};
            }

            return hook;
        }
    };

#else
    #include "inlinehook.h"

    struct HookInline {
        void* target   = nullptr;
        void* original = nullptr;
        hook_handle* handle = nullptr;

        template<typename Ret = void, typename... Args>
        Ret call(Args... args) const {
            return reinterpret_cast<Ret(*)(Args...)>(original)(args...);
        }

        explicit operator bool() const noexcept { return original != nullptr; }
    };

    namespace hooker {
        inline HookInline create_inline(void* target, void* detour, int flags = 0) {
            HookInline hook;
            hook.target = target;

            void* orig = nullptr;

            hook.handle = hook_addr(
                target,
                detour,
                &orig,
                flags
            );

            if (!hook.handle || !orig)
                return {};

            hook.original = orig;
            return hook;
        }
    };
#endif