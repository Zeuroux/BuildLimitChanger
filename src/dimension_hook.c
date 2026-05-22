#include "dimension_hook.h"

#include "config/config_manager.h"
#include "hook.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#define BLC_ALIGNOF(T) __alignof(T)
#else
#define BLC_ALIGNOF(T) _Alignof(T)
#endif

typedef struct BlcStringView {
    const char *data;
    size_t size;
} BlcStringView;

#if defined(_MSC_VER)
typedef struct PlatformString {
    union {
        char buffer[16];
        char *ptr;
    } storage;
    size_t size;
    size_t capacity;
} PlatformString;
_Static_assert(sizeof(PlatformString) == 32, "MSVC string size mismatch");

#elif defined(__GNUC__) || defined(__clang__)
typedef struct PlatformString {
    union {
        struct {
            size_t capacity;
            size_t size;
            char *ptr;
        } long_str;
        struct {
            unsigned char size_tag;
            char data[23];
        } short_str;
        uint8_t raw[24];
    } value;
} PlatformString;
_Static_assert(sizeof(PlatformString) == 24, "platform string size mismatch");
#else
#error Unsupported compiler
#endif

typedef struct DimensionArguments {
    int32_t dim_id;
    DimensionHeightRange height_range;
    PlatformString name;
} DimensionArguments;

static HookInline dimension_hook;

static BlcStringView platform_string_view(const PlatformString *string)
{
    BlcStringView view = { NULL, 0 };

    if (!string)
        return view;

#if defined(_MSC_VER)
    view.size = string->size;
    if (string->capacity >= 16) {
        if ((uintptr_t)string->storage.ptr < 0x10000u)
            return (BlcStringView){ NULL, 0 };
        view.data = string->storage.ptr;
    } else {
        view.data = string->storage.buffer;
    }
#else
    if (string->value.raw[0] & 0x01u) {
        if ((uintptr_t)string->value.long_str.ptr < 0x10000u)
            return (BlcStringView){ NULL, 0 };
        view.data = string->value.long_str.ptr;
        view.size = string->value.long_str.size;
    } else {
        view.data = (const char *)&string->value.raw[1];
        view.size = (size_t)(string->value.raw[0] >> 1u);
    }
#endif

    return view;
}

static bool height_range_valid(DimensionHeightRange range)
{
    int16_t hi = range.max;
    int16_t lo = range.min;

    return lo >= -2048 && lo <= 2048 &&
           hi >= -2048 && hi <= 2048 &&
           lo % 16 == 0 && hi % 16 == 0 &&
           hi > lo;
}

static bool name_view_valid(BlcStringView view)
{
    if (!view.data || view.size == 0 || view.size > 128)
        return false;

    for (size_t i = 0; i < view.size; ++i) {
        unsigned char c = (unsigned char)view.data[i];
        if (c < 32 || c == 127)
            return false;
    }

    return true;
}

#if !defined(_MSC_VER) && !defined(__ANDROID__)
typedef struct GnuString {
    const char *data;
    size_t size;
    union {
        char local[16];
        size_t capacity;
    } storage;
} GnuString;

static BlcStringView gnu_string_view(const void *string)
{
    const GnuString *value = (const GnuString *)string;
    BlcStringView view = { NULL, 0 };

    if (!value || (uintptr_t)value->data < 0x10000u)
        return view;

    view.data = value->data;
    view.size = value->size;
    return view;
}
#endif

static bool name_view_plausible(BlcStringView view)
{
    return view.data && view.size > 0 && view.size <= 128 && (uintptr_t)view.data >= 0x10000u;
}

static BlcStringView hook_name_view(const void *name)
{
#if defined(_MSC_VER) || defined(__ANDROID__)
    return platform_string_view((const PlatformString *)name);
#else
    BlcStringView view = gnu_string_view(name);
    if (name_view_plausible(view))
        return view;
    return platform_string_view((const PlatformString *)name);
#endif
}

static bool dimension_arguments_valid(const DimensionArguments *args)
{
    return args &&
           height_range_valid(args->height_range) &&
           name_view_valid(platform_string_view(&args->name));
}

static DimensionArguments *find_dimension_arguments(uint8_t *region, size_t region_size, size_t offset)
{
    const size_t k_size = sizeof(DimensionArguments);
    const uintptr_t k_align_mask = (uintptr_t)BLC_ALIGNOF(DimensionArguments) - 1u;

    if (!region || region_size < k_size || offset > region_size - k_size)
        return NULL;

    for (size_t i = offset; i <= region_size - k_size; ++i) {
        uint8_t *candidate_ptr = region + i;
        DimensionArguments candidate;

        if (((uintptr_t)candidate_ptr & k_align_mask) != 0u)
            continue;

        memcpy(&candidate, candidate_ptr, k_size);
        if (dimension_arguments_valid(&candidate))
            return (DimensionArguments *)candidate_ptr;
    }

    return NULL;
}

typedef void (BLC_CALL *DimensionV2Original)(void *self, void *dimension_arguments);
typedef void (BLC_CALL *DimensionV1Original)(void *self, void *level, int32_t dim_id,
                                             int32_t height_range, void *callback_context, void *name);
typedef void (BLC_CALL *DimensionV0Original)(void *self, void *level, int32_t dim_id,
                                             int16_t height_max, void *callback_context, void *name);

static void BLC_CALL dimension_v2(void *self, void *dimension_arguments)
{
    DimensionArguments *args = find_dimension_arguments((uint8_t *)dimension_arguments, 256, 70);

    if (args) {
        BlcStringView name = platform_string_view(&args->name);
        DimensionHeightRange range = args->height_range;
        DimensionHeightRange config = config_manager_get_dimension(name.data, name.size, range.min, range.max);
        args->height_range.raw = config.raw;
    }

    if (dimension_hook.original)
        ((DimensionV2Original)dimension_hook.original)(self, dimension_arguments);
}

static void BLC_CALL dimension_v1(void *self, void *level, int32_t dim_id,
                                  int32_t height_range, void *callback_context, void *name)
{
    DimensionHeightRange range;
    BlcStringView dimension_name = hook_name_view(name);
    DimensionHeightRange config;

    range.raw = height_range;
    config.raw = range.raw;

    if (name_view_valid(dimension_name))
        config = config_manager_get_dimension(dimension_name.data, dimension_name.size, range.min, range.max);

    if (dimension_hook.original)
        ((DimensionV1Original)dimension_hook.original)(self, level, dim_id, config.raw, callback_context, name);
}

static void BLC_CALL dimension_v0(void *self, void *level, int32_t dim_id,
                                  int16_t height_max, void *callback_context, void *name)
{
    BlcStringView dimension_name = hook_name_view(name);
    DimensionHeightRange config;

    config.min = 0;
    config.max = height_max;

    if (name_view_valid(dimension_name))
        config = config_manager_get_dimension(dimension_name.data, dimension_name.size, 0, height_max);

    if (config.max > 256)
        config.max = 256;

    if (dimension_hook.original)
        ((DimensionV0Original)dimension_hook.original)(self, level, dim_id, config.max, callback_context, name);
}

void setup_hook(void *func, int hook_version)
{
    static void *dimension_hooks[] = {
        (void *)&dimension_v0,
        (void *)&dimension_v1,
        (void *)&dimension_v2,
    };

    if (!func || hook_version < 0 || hook_version >= (int)(sizeof(dimension_hooks) / sizeof(dimension_hooks[0])))
        return;

    dimension_hook = hook_inline_create(func, dimension_hooks[hook_version], 0);
}
