#pragma once

#include <stdint.h>

typedef union DimensionHeightRange {
    int32_t raw;
    struct {
        int16_t min;
        int16_t max;
    };
} DimensionHeightRange;

void setup_hook(void *func, int hook_version);
