#pragma once

#if defined(__linux__)

#include "section_parser.h"

#include <elf.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static bool elf_in_bounds(size_t file_size, size_t offset, size_t len)
{
    return offset <= file_size && len <= file_size - offset;
}

static bool parse_elf_sections(const char *path, uintptr_t load_bias, SectionList *out)
{
    int fd;
    struct stat st;
    size_t file_size;
    void *base;
    const unsigned char *ident;
    const Elf64_Ehdr *ehdr;
    size_t sh_off;
    size_t sh_num;
    size_t sh_size;
    size_t str_idx;
    const Elf64_Shdr *strtab_hdr;
    size_t strtab_off;
    size_t strtab_size;
    const char *strtab;
    bool ok = true;

    if (!path || !out)
        return false;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;

    if (fstat(fd, &st) < 0) {
        close(fd);
        return false;
    }

    file_size = (size_t)st.st_size;
    base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED)
        return false;

    ident = (const unsigned char *)base;
    if (file_size < EI_NIDENT || memcmp(ident, ELFMAG, SELFMAG) != 0 ||
        ident[EI_CLASS] != ELFCLASS64 ||
        !elf_in_bounds(file_size, 0, sizeof(Elf64_Ehdr))) {
        munmap(base, file_size);
        return false;
    }

    ehdr = (const Elf64_Ehdr *)base;
    sh_off = (size_t)ehdr->e_shoff;
    sh_num = (size_t)ehdr->e_shnum;
    sh_size = (size_t)ehdr->e_shentsize;
    str_idx = (size_t)ehdr->e_shstrndx;

    if (sh_off == 0 || sh_num == 0 || sh_size < sizeof(Elf64_Shdr) ||
        sh_num > SIZE_MAX / sh_size ||
        !elf_in_bounds(file_size, sh_off, sh_num * sh_size) ||
        str_idx == SHN_UNDEF || str_idx >= sh_num) {
        munmap(base, file_size);
        return false;
    }

    strtab_hdr = (const Elf64_Shdr *)((const uint8_t *)base + sh_off + str_idx * sh_size);
    strtab_off = (size_t)strtab_hdr->sh_offset;
    strtab_size = (size_t)strtab_hdr->sh_size;
    if (!elf_in_bounds(file_size, strtab_off, strtab_size)) {
        munmap(base, file_size);
        return false;
    }

    strtab = (const char *)base + strtab_off;

    for (size_t i = 0; i < sh_num; ++i) {
        const Elf64_Shdr *section =
            (const Elf64_Shdr *)((const uint8_t *)base + sh_off + i * sh_size);
        const char *name;

        if (section->sh_type == SHT_NULL || section->sh_size == 0)
            continue;
        if (section->sh_name >= strtab_size)
            continue;

        name = strtab + section->sh_name;
        if (!section_list_push(out, name, strlen(name),
                               load_bias + (uintptr_t)section->sh_addr,
                               (size_t)section->sh_size)) {
            ok = false;
            break;
        }
    }

    munmap(base, file_size);
    return ok;
}
#endif
