#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../dimension_hook.h"

void config_manager_init(const char *folder_path);
DimensionHeightRange config_manager_get_dimension(const char *name, size_t name_len,
                                                  int16_t def_min, int16_t def_max);
int config_manager_get_hook_version_override(void);
