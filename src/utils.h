#pragma once

#include <stdbool.h>
#include <stddef.h>

const char *get_executable_dir(void);

#if defined(__ANDROID__)
bool get_config_location(char *out_path, size_t out_size);
#endif

#if defined(_WIN32)
const char *get_state_directory(void);
#endif
