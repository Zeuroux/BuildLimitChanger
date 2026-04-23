#include "section_parser.hpp"

#include <cstring>

void *SectionInfo::find_string(const char *needle) const
{
    if (!needle || size == 0)
        return nullptr;

    const auto *haystack = reinterpret_cast<const uint8_t *>(addr);
    const auto *n = reinterpret_cast<const uint8_t *>(needle);
    const size_t needle_len = std::strlen(needle);

    if (needle_len == 0 || size < needle_len)
        return nullptr;

    const auto *cursor = haystack;
    const auto *end = haystack + (size - needle_len + 1);

    while (cursor < end) {
        cursor = static_cast<const uint8_t *>(std::memchr(cursor, n[0], static_cast<size_t>(end - cursor)));
        if (!cursor)
            return nullptr;

        if (needle_len == 1 || std::memcmp(cursor + 1, n + 1, needle_len - 1) == 0)
            return const_cast<uint8_t *>(cursor);

        ++cursor;
    }

    return nullptr;
}

#if defined(__x86_64__) || defined(_M_X64)
#include "../decoder/x64_decoder.hpp"

static uint32_t read_u32_unaligned(const uint8_t *ptr)
{
    uint32_t value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

void *SectionInfo::find_refs(void *ptr) const
{
    if (!ptr || size < sizeof(uint32_t))
        return nullptr;

    const uint8_t *data = reinterpret_cast<const uint8_t *>(addr);
    const uint64_t target = reinterpret_cast<uint64_t>(ptr);

    size_t search_start = 0;
    const size_t limit = size - sizeof(uint32_t);

    while (search_start <= limit)
    {
        size_t match_offset = SIZE_MAX;
        uint32_t expected_disp = static_cast<uint32_t>(target - addr - 4 - search_start);

        for (size_t i = search_start; i <= limit; ++i, --expected_disp)
        {
            if (read_u32_unaligned(data + i) == expected_disp)
            {
                match_offset = i;
                break;
            }
        }

        if (match_offset == SIZE_MAX) break;

        size_t decode_start = search_start;
        size_t cc_count = 0;
        for (size_t i = match_offset; i > search_start; --i)
        {
            if (data[i] == 0xCC)
            {
                if (++cc_count >= 3)
                {
                    decode_start = i + 1;
                    while (decode_start < match_offset && data[decode_start] == 0xCC)
                        decode_start++;
                    break;
                }
            }
            else
            {
                cc_count = 0;
            }
        }

        using namespace x64_decoder;
        std::span<const uint8_t> bytes(data + decode_start, size - decode_start);
        Decoder decoder(bytes, addr + decode_start);
        Instruction instr;
        State state = SCAN_PROLOGUE;
        uint64_t current_fn_ip = 0;
        const uint64_t max_ip = addr + match_offset + 4;

        while (decoder.decode_next(instr))
        {
            if (state != FOUND_REF && instr.ip() > max_ip) break;

            const auto mnemonic = instr.mnemonic();
            switch (state)
            {
            case SCAN_PROLOGUE:
                if (mnemonic == Mnemonic::PUSH || mnemonic == Mnemonic::MOV)
                {
                    current_fn_ip = instr.ip();
                    state = IN_FUNCTION;
                }
                break;
            case IN_FUNCTION:
                if (mnemonic == Mnemonic::RET)
                {
                    state = SCAN_PROLOGUE;
                    break;
                }
                if (instr.memory_base() == Register::RIP && instr.ip_rel_memory_address() == target)
                {
                    state = FOUND_REF;
                }
                break;
            case FOUND_REF:
                if (mnemonic == Mnemonic::RET)
                {
                    return reinterpret_cast<void *>(current_fn_ip);
                }
                break;
            }
        }
        search_start = match_offset + 1;
    }
    return nullptr;
}
#elif defined(__aarch64__)
void *SectionInfo::find_refs(void *ptr) const
{
    if (!ptr || size < 4) return nullptr;

    const uint64_t target = reinterpret_cast<uint64_t>(ptr);
    const uint32_t * __restrict__ insns = reinterpret_cast<const uint32_t *>(addr);
    const size_t count = size >> 2;

    State state = SCAN_PROLOGUE;
    uint64_t current_fn_ip = 0;

    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t insn = insns[i];

        if (__builtin_expect(insn == 0xD65F03C0, 0))
        {
            if (state == FOUND_REF)
                return reinterpret_cast<void *>(current_fn_ip);
            state = SCAN_PROLOGUE;
            continue;
        }

        if (__builtin_expect((insn & 0x9F000000) == 0x90000000, 0))
        {
            if (state == IN_FUNCTION && i + 1 < count)
            {
                const uint32_t next = insns[i + 1];
                if ((next & 0xFFC00000) == 0x91000000)
                {
                    const uint32_t rd = insn & 0x1Fu;
                    if ((next & 0x1Fu) == rd && ((next >> 5) & 0x1Fu) == rd)
                    {
                        const int64_t imm21 =
                            (static_cast<int64_t>((insn >> 5) & 0x7FFFFu) << 2) |
                             static_cast<int64_t>((insn >> 29) & 0x3u);

                        const uint64_t page = (addr + i * 4u) & ~0xFFFull;
                        const uint64_t adrp_val = page + static_cast<uint64_t>((imm21 << 43) >> 31);

                        const uint32_t imm12 = (next >> 10) & 0xFFFu;
                        const uint64_t resolved = adrp_val +
                            (((next >> 22) & 0x3u) == 1u
                                ? static_cast<uint64_t>(imm12) << 12
                                : static_cast<uint64_t>(imm12));

                        if (resolved == target)
                            state = FOUND_REF;

                        ++i; 
                    }
                }
            }
            continue;
        }

        if (__builtin_expect(
                ((insn & 0xFFC07FFFu) == 0xA9807BFDu) |
                ((insn & 0xFFC003FFu) == 0xD10003FFu), 0))
        {
            if (state == SCAN_PROLOGUE)
            {
                current_fn_ip = addr + i * 4u;
                state = IN_FUNCTION;
            }
            continue;
        }
    }
    return nullptr;
}
#else
#error Unsupported
#endif
