#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "section_parser.h"

#include <string.h>

bool section_list_push(SectionList *list, const char *name, size_t name_len,
                       uintptr_t addr, size_t size)
{
    SectionInfo *section;
    size_t copy_len;

    if (!list || !name || list->count >= SECTION_LIST_MAX)
        return false;

    copy_len = name_len < SECTION_NAME_MAX ? name_len : SECTION_NAME_MAX - 1;
    section = &list->items[list->count++];
    memcpy(section->name, name, copy_len);
    section->name[copy_len] = '\0';
    section->addr = addr;
    section->size = size;
    return true;
}

#if defined(__linux__) || defined(__ANDROID__)
#include "elf_parser.h"

#include <limits.h>
#include <link.h>
#include <stdbool.h>
#include <unistd.h>

#define MC_SO_NAME "libminecraftpe.so"

typedef struct IterCtx {
    const char *target_path;
    const char *target_name;
    uintptr_t base_addr;
    char file_path[PATH_MAX];
    bool match_main_executable;
    bool found;
} IterCtx;

static const char *base_name(const char *path)
{
    const char *base;

    if (!path || path[0] == '\0')
        return "";

    base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static int phdr_callback(struct dl_phdr_info *info, size_t size, void *data)
{
    IterCtx *ctx = (IterCtx *)data;
    const char *image_name = info->dlpi_name;
    bool matched;
    (void)size;

    if (!image_name || image_name[0] == '\0')
        matched = ctx->match_main_executable;
    else
        matched = strcmp(ctx->target_name, base_name(image_name)) == 0;

    if (!matched)
        return 0;

    ctx->base_addr = (uintptr_t)info->dlpi_addr;
    ctx->file_path[0] = '\0';
    if (image_name && image_name[0] != '\0') {
        strncpy(ctx->file_path, image_name, sizeof(ctx->file_path) - 1);
        ctx->file_path[sizeof(ctx->file_path) - 1] = '\0';
    }
    ctx->found = true;
    return 1;
}

bool get_sections_runtime(SectionList *out)
{
    IterCtx ctx;
    const char *path;

    if (!out)
        return false;

#if defined(__ANDROID__)
    memset(&ctx, 0, sizeof(ctx));
    ctx.target_path = MC_SO_NAME;
    ctx.target_name = MC_SO_NAME;
#else
    char exe_path[PATH_MAX];
    ssize_t n;

    memset(exe_path, 0, sizeof(exe_path));
    n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n > 0)
        exe_path[n] = '\0';
    else
        strncpy(exe_path, "/proc/self/exe", sizeof(exe_path) - 1);

    memset(&ctx, 0, sizeof(ctx));
    ctx.target_path = exe_path;
    ctx.target_name = base_name(exe_path);
    ctx.match_main_executable = true;
#endif

    dl_iterate_phdr(phdr_callback, &ctx);
    if (!ctx.found)
        return false;

    path = ctx.file_path[0] != '\0' ? ctx.file_path : ctx.target_path;
    return parse_elf_sections(path, ctx.base_addr, out);
}

#elif defined(_WIN32)

#define NOMINMAX
#include <windows.h>
#include <psapi.h>

static size_t blc_strnlen(const char *value, size_t max_len)
{
    size_t len = 0;

    while (len < max_len && value[len] != '\0')
        ++len;

    return len;
}

bool get_sections_runtime(SectionList *out)
{
    HMODULE module;
    MODULEINFO module_info;
    uintptr_t base_addr;
    const BYTE *image;
    const IMAGE_DOS_HEADER *dos_header;
    const IMAGE_NT_HEADERS *nt_headers;
    const IMAGE_SECTION_HEADER *sections;

    if (!out)
        return false;

    module = GetModuleHandleW(NULL);
    if (!module)
        return false;

    memset(&module_info, 0, sizeof(module_info));
    if (!GetModuleInformation(GetCurrentProcess(), module, &module_info, sizeof(module_info)))
        return false;

    base_addr = (uintptr_t)module_info.lpBaseOfDll;
    image = (const BYTE *)base_addr;

    dos_header = (const IMAGE_DOS_HEADER *)image;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    nt_headers = (const IMAGE_NT_HEADERS *)(image + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
        return false;

    sections = IMAGE_FIRST_SECTION(nt_headers);
    for (WORD i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER *section = &sections[i];
        const char *name = (const char *)section->Name;
        size_t name_len = blc_strnlen(name, IMAGE_SIZEOF_SHORT_NAME);

        if (!section_list_push(out, name, name_len,
                               base_addr + section->VirtualAddress,
                               (size_t)section->Misc.VirtualSize))
            return false;
    }

    return true;
}
#endif
