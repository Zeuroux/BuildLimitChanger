#ifdef __linux__
#pragma once
#include "section_parser.hpp"
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>

inline std::vector<SectionInfo> parse_elf_sections(
    const char* path,
    uintptr_t   load_bias)
{
    std::vector<SectionInfo> out;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return out;

    struct stat st{};
    if (fstat(fd, &st) < 0) { close(fd); return out; }

    size_t file_size = static_cast<size_t>(st.st_size);
    void*  base      = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) return out;

    auto cleanup = [&]{ munmap(base, file_size); };
    auto in_bounds = [&](size_t off, size_t len) -> bool {
        return off <= file_size && len <= file_size - off;
    };

    auto* ident = reinterpret_cast<const unsigned char*>(base);
    if (file_size < EI_NIDENT || memcmp(ident, ELFMAG, SELFMAG) != 0) {
        cleanup(); return out;
    }

    if (ident[EI_CLASS] != ELFCLASS64) { cleanup(); return out; }
    {
        if (!in_bounds(0, sizeof(Elf64_Ehdr))) { cleanup(); return out; }
        auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(base);

        size_t sh_off  = ehdr->e_shoff;
        size_t sh_num  = ehdr->e_shnum;
        size_t sh_size = ehdr->e_shentsize;
        size_t str_idx = ehdr->e_shstrndx;

        if (sh_off == 0 || sh_num == 0 || sh_size < sizeof(Elf64_Shdr)) { cleanup(); return out; }
        if (!in_bounds(sh_off, sh_num * sh_size))                        { cleanup(); return out; }
        if (str_idx == SHN_UNDEF || str_idx >= sh_num)                   { cleanup(); return out; }

        auto shdr = [&](size_t i) -> const Elf64_Shdr* {
            return reinterpret_cast<const Elf64_Shdr*>(
                static_cast<const uint8_t*>(base) + sh_off + i * sh_size);
        };

        const Elf64_Shdr* strtab_hdr  = shdr(str_idx);
        size_t            strtab_off  = strtab_hdr->sh_offset;
        size_t            strtab_size = strtab_hdr->sh_size;
        if (!in_bounds(strtab_off, strtab_size)) { cleanup(); return out; }

        const char* strtab = reinterpret_cast<const char*>(
            static_cast<const uint8_t*>(base) + strtab_off);

        for (size_t i = 0; i < sh_num; i++) {
            const Elf64_Shdr* sh = shdr(i);
            if (sh->sh_type == SHT_NULL || sh->sh_size == 0) continue;
            if (sh->sh_name >= strtab_size)                   continue;

            const char* name = strtab + sh->sh_name;
            out.push_back({
                name,
                load_bias + static_cast<uintptr_t>(sh->sh_addr),
                static_cast<size_t>(sh->sh_size)
            });
        }
    }

    cleanup();
    return out;
}
#endif