#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum State : uint8_t
{
    SCAN_PROLOGUE = 0,
    IN_FUNCTION = 1,
    FOUND_REF = 2
};

struct SectionInfo
{
    std::string name;
    uintptr_t addr;
    size_t size;

    void *find_string(const char *needle) const;

    void *find_refs(void *ptr) const;
};

std::vector<SectionInfo> get_sections_runtime();