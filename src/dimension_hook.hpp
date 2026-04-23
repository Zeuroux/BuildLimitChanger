#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#if defined(_MSC_VER) 
struct PlatformString {
    union {
        char buffer[16];
        char* ptr;
    } storage;
    size_t size;
    size_t capacity;

    [[nodiscard]] const char* get_data() const noexcept {
        return (capacity >= 16) ? storage.ptr : storage.buffer;
    }

    [[nodiscard]] std::string_view view() const noexcept {
        if (capacity >= 16) {
            if (reinterpret_cast<uintptr_t>(storage.ptr) < 0x10000u) {
                return {};
            }
            return { storage.ptr, size };
        }
        return { storage.buffer, size };
    }
};
static_assert(sizeof(PlatformString) == 32, "MSVC string size mismatch");

#elif defined(__GNUC__) || defined(__clang__)
struct PlatformString {
    union {
        struct { size_t capacity, size; char* ptr; } long_str;
        struct { unsigned char is_long : 1, size_payload : 7; char data[23]; } short_str;
        uint8_t raw[24];
    };
    [[nodiscard]] char* get_data() noexcept {
        return (raw[0] & 0x01u)
            ? long_str.ptr
            : reinterpret_cast<char*>(&raw[1]);
    }
    [[nodiscard]] const char* get_data() const noexcept {
        return (raw[0] & 0x01u)
            ? long_str.ptr
            : reinterpret_cast<const char*>(&raw[1]);
    }
    [[nodiscard]] size_t get_size() const noexcept {
        return (raw[0] & 0x01u) ? long_str.size : static_cast<size_t>(raw[0] >> 1u);
    }
    [[nodiscard]] std::string_view view() const noexcept {
        return { get_data(), get_size() };
    }
};
static_assert(sizeof(PlatformString) == 24, "GCC string size mismatch");
#else
#error "Unsupported"
#endif

union DimensionHeightRange {
    int32_t raw;
    struct { int16_t min, max; };
};

struct DimensionArguments {
    int32_t              dimId;
    DimensionHeightRange heightRange;
    PlatformString       name;

    [[nodiscard]] static DimensionArguments*
    fromSpan(std::span<std::byte> region, size_t offset = 0) noexcept;

    [[nodiscard]] static DimensionArguments*
    fromPtr(void* base, size_t scanSize, size_t scanOffset) noexcept;

    [[nodiscard]] const char* getValidName() noexcept { return name.get_data(); }

private:
    [[nodiscard]] bool heightRangeValid() const noexcept {
        auto hi = heightRange.max;
        auto lo = heightRange.min;
        return lo >= -2048 && lo <= 2048
            && hi >= -2048 && hi <= 2048
            && lo % 16 == 0 && hi % 16 == 0
            && hi > lo;
    }
    
    [[nodiscard]] bool nameValid() const noexcept {
        auto sv = name.view();
        if (sv.empty()) return false;

        for (unsigned char c : sv) {
            if (c < 32 || c == 127) return false;
        }
        return true;
    }
};

void setup_hook(void* func, int hookVersion);
