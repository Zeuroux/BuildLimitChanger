#pragma once

#include <stdbool.h>

#if defined(_WIN32)
#define BLC_CALL __cdecl
#include <MinHook.h>

typedef struct HookInline {
    void *target;
    void *original;
} HookInline;

static inline MH_STATUS hook_ensure_init(void)
{
    static bool initialized = false;
    static MH_STATUS status = MH_OK;

    if (!initialized) {
        status = MH_Initialize();
        initialized = true;
    }

    return status;
}

static inline HookInline hook_inline_create(void *target, void *detour, int flags)
{
    HookInline hook;
    (void)flags;

    hook.target = target;
    hook.original = NULL;

    if (hook_ensure_init() != MH_OK)
        return (HookInline){ NULL, NULL };

    if (MH_CreateHook(target, detour, &hook.original) != MH_OK)
        return (HookInline){ NULL, NULL };

    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        return (HookInline){ NULL, NULL };
    }

    return hook;
}

#else
#define BLC_CALL
#include "inlinehook.h"

typedef struct HookInline {
    void *target;
    void *original;
    hook_handle *handle;
} HookInline;

static inline HookInline hook_inline_create(void *target, void *detour, int flags)
{
    HookInline hook;
    void *original = NULL;

    hook.target = target;
    hook.original = NULL;
    hook.handle = hook_addr(target, detour, &original, flags);

    if (!hook.handle || !original)
        return (HookInline){ NULL, NULL, NULL };

    hook.original = original;
    return hook;
}
#endif
