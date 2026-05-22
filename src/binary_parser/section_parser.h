#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum ScanState {
    SCAN_PROLOGUE = 0,
    IN_FUNCTION = 1,
    FOUND_REF = 2
} ScanState;

#define SECTION_NAME_MAX 64
#define SECTION_LIST_MAX 256

typedef struct SectionInfo {
    char name[SECTION_NAME_MAX];
    uintptr_t addr;
    size_t size;
} SectionInfo;

typedef struct SectionList {
    SectionInfo items[SECTION_LIST_MAX];
    size_t count;
} SectionList;

bool section_list_push(SectionList *list, const char *name, size_t name_len,
                       uintptr_t addr, size_t size);

void *section_find_string(const SectionInfo *section, const char *needle);
void *section_find_refs(const SectionInfo *section, void *ptr);

bool get_sections_runtime(SectionList *out);
