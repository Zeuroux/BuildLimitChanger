#include "section_parser.hpp"

#if defined(__linux__) || defined(__ANDROID__)
#include "elf_parser.hpp"
#include <cstring>
#include <linux/limits.h>
#include <link.h>
#include <unistd.h>

#define MC_SO_NAME "libminecraftpe.so"

struct IterCtx {
    std::string target_path;
    std::string target_name;
    uintptr_t base_addr{};
    std::string file_path;
    bool match_main_executable{};
};

static const char* base_name(const char* path)
{
    if (!path || *path == '\0')
        return "";

    if (const char* base = std::strrchr(path, '/'))
        return base + 1;

    return path;
}

static int phdr_callback(dl_phdr_info* info, size_t, void* data)
{
    auto* ctx = reinterpret_cast<IterCtx*>(data);
    const char* imageName = info->dlpi_name;
    bool matched = false;

    if (!imageName || *imageName == '\0')
        matched = ctx->match_main_executable;
    else
        matched = ctx->target_name == base_name(imageName);

    if (!matched) return 0;

    ctx->base_addr = static_cast<uintptr_t>(info->dlpi_addr);
    ctx->file_path = (imageName && *imageName != '\0') ? imageName : ctx->target_path;
    return 1;
}

std::vector<SectionInfo> get_sections_runtime()
{
#if defined(__ANDROID__)
    IterCtx ctx{ MC_SO_NAME, MC_SO_NAME };
#else
    char    exe_buf[PATH_MAX]{};
    ssize_t n = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    std::string exePath = n > 0 ? std::string(exe_buf, static_cast<size_t>(n)) : "/proc/self/exe";
    IterCtx ctx{ exePath, base_name(exePath.c_str()), 0, {}, true };
#endif

    dl_iterate_phdr(phdr_callback, &ctx);

    if (ctx.base_addr == 0)
        return {};

    return parse_elf_sections(
        (ctx.file_path.empty() ? ctx.target_path : ctx.file_path).c_str(),
        ctx.base_addr);
}

#elif defined(_WIN32)

#include <windows.h>
#include <psapi.h>
#include <stdexcept>
#include <vector>

std::vector<SectionInfo> get_sections_runtime()
{
    HMODULE hModule = GetModuleHandleW(nullptr);
    if (!hModule)
        throw std::runtime_error("Failed to get module handle for main executable");

    MODULEINFO modInfo = {};
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo)))
        throw std::runtime_error("GetModuleInformation failed");

    const auto baseAddr  = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    const auto* imagePtr = reinterpret_cast<const BYTE*>(baseAddr);

    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(imagePtr);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        throw std::runtime_error("Invalid DOS signature");

    const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        imagePtr + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        throw std::runtime_error("Invalid NT signature");

    const WORD sectionCount = ntHeaders->FileHeader.NumberOfSections;
    const auto* sections    = IMAGE_FIRST_SECTION(ntHeaders);

    std::vector<SectionInfo> result;
    result.reserve(sectionCount);

    for (WORD i = 0; i < sectionCount; ++i) {
        const IMAGE_SECTION_HEADER& s = sections[i];

        std::string name(reinterpret_cast<const char*>(s.Name),
                         strnlen(reinterpret_cast<const char*>(s.Name), IMAGE_SIZEOF_SHORT_NAME));

        result.push_back({
            std::move(name),
            baseAddr + s.VirtualAddress,
            s.Misc.VirtualSize
        });
    }

    return result;
}

#endif
