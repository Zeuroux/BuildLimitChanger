#include "dimension_hook.hpp"

#include <array>
#include <cstring>
#include <string>

DimensionArguments* DimensionArguments::fromSpan(std::span<std::byte> region, size_t offset) noexcept {
    constexpr size_t kSize = sizeof(DimensionArguments);
    constexpr uintptr_t kAlignMask = alignof(DimensionArguments) - 1u;

    if (region.size() < kSize || offset > region.size() - kSize)
        return nullptr;

    auto* search = region.data() + offset;
    const size_t searchSize = region.size() - offset - kSize + 1;

    for (size_t i = 0; i < searchSize; ++i) {
        auto* candidatePtr = search + i;
        if ((reinterpret_cast<uintptr_t>(candidatePtr) & kAlignMask) != 0u)
            continue;

        DimensionArguments candidate{};
        std::memcpy(&candidate, candidatePtr, kSize);
        if (candidate.heightRangeValid() && candidate.nameValid())
            return reinterpret_cast<DimensionArguments*>(candidatePtr);
    }

    return nullptr;
}

DimensionArguments* DimensionArguments::fromPtr(void* base, size_t scanSize, size_t scanOffset) noexcept {
    if (!base) return nullptr;
    return fromSpan({ static_cast<std::byte*>(base), scanSize }, scanOffset);
}

#include "config/config_manager.hpp"
#include "hook.hpp"

static HookInline dimension_hook;

static void Dimension_v2(void *self, void* dimensionArguments)
{
    auto args = DimensionArguments::fromPtr(dimensionArguments, 256, 70);
    if (args != nullptr){
        auto hr = args->heightRange;
        auto config = ConfigManager::getInstance().getDimension(args->getValidName(), hr.min, hr.max);
        args->heightRange.raw = config.raw;
    }
    dimension_hook.call(self, dimensionArguments);
}

static void Dimension_v1(void *self, void *level, int32_t dimId, int32_t heightRange, void* callbackContext, void* name)
{
    auto& str = *reinterpret_cast<std::string*>(name);
    DimensionHeightRange hr{ heightRange };
    auto config = ConfigManager::getInstance().getDimension(str, hr.min, hr.max);
    dimension_hook.call(self, level, dimId, config.raw, callbackContext, name);
}

static void Dimension_v0(void *self, void *level, int32_t dimId, int16_t heightMax, void *callbackContext, void* name)
{
    auto& str = *reinterpret_cast<std::string*>(name);
    auto config = ConfigManager::getInstance().getDimension(str, 0, heightMax);

    if (config.max > 256)
        config.max = 256;

    dimension_hook.call(self, level, dimId, config.max, callbackContext, name);
}


void setup_hook(void* func, int hookVersion)
{
    static const std::array<void*, 3> dimensionHooks = {
        (void *)&Dimension_v0,
        (void *)&Dimension_v1,
        (void *)&Dimension_v2,
    };
    
    dimension_hook = hooker::create_inline(func, dimensionHooks[hookVersion]);
}
