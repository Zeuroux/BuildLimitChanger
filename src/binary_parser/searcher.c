#include "section_parser.h"

#include <stdint.h>
#include <string.h>

void *section_find_string(const SectionInfo *section, const char *needle)
{
    const uint8_t *haystack;
    const uint8_t *n;
    size_t needle_len;
    const uint8_t *cursor;
    const uint8_t *end;

    if (!section || !needle || section->size == 0)
        return NULL;

    haystack = (const uint8_t *)section->addr;
    n = (const uint8_t *)needle;
    needle_len = strlen(needle);

    if (needle_len == 0 || section->size < needle_len)
        return NULL;

    cursor = haystack;
    end = haystack + (section->size - needle_len + 1);

    while (cursor < end) {
        cursor = (const uint8_t *)memchr(cursor, n[0], (size_t)(end - cursor));
        if (!cursor)
            return NULL;

        if (needle_len == 1 || memcmp(cursor + 1, n + 1, needle_len - 1) == 0)
            return (void *)cursor;

        ++cursor;
    }

    return NULL;
}

#if defined(__x86_64__) || defined(_M_X64)
#include "../decoder/x64_decoder.h"

static uint32_t read_u32_unaligned(const uint8_t *ptr)
{
    uint32_t value;
    memcpy(&value, ptr, sizeof(value));
    return value;
}

void *section_find_refs(const SectionInfo *section, void *ptr)
{
    const uint8_t *data;
    uint64_t target;
    size_t search_start = 0;
    size_t limit;

    if (!section || !ptr || section->size < sizeof(uint32_t))
        return NULL;

    data = (const uint8_t *)section->addr;
    target = (uint64_t)(uintptr_t)ptr;
    limit = section->size - sizeof(uint32_t);

    while (search_start <= limit) {
        size_t match_offset = SIZE_MAX;
        uint32_t expected_disp = (uint32_t)(target - section->addr - 4 - search_start);

        for (size_t i = search_start; i <= limit; ++i, --expected_disp) {
            if (read_u32_unaligned(data + i) == expected_disp) {
                match_offset = i;
                break;
            }
        }

        if (match_offset == SIZE_MAX)
            break;

        {
            size_t decode_start = search_start;
            size_t cc_count = 0;
            X64Decoder decoder;
            X64Instruction instruction;
            ScanState state = SCAN_PROLOGUE;
            uint64_t current_fn_ip = 0;
            uint64_t max_ip = section->addr + match_offset + 4;

            for (size_t i = match_offset; i > search_start; --i) {
                if (data[i] == 0xCC) {
                    if (++cc_count >= 3) {
                        decode_start = i + 1;
                        while (decode_start < match_offset && data[decode_start] == 0xCC)
                            ++decode_start;
                        break;
                    }
                } else {
                    cc_count = 0;
                }
            }

            x64_decoder_init(&decoder, data + decode_start, section->size - decode_start,
                             section->addr + decode_start);

            while (x64_decoder_next(&decoder, &instruction)) {
                X64Mnemonic mnemonic = instruction.mnemonic;

                if (state != FOUND_REF && instruction.ip > max_ip)
                    break;

                switch (state) {
                case SCAN_PROLOGUE:
                    if (mnemonic == X64_MNEMONIC_PUSH || mnemonic == X64_MNEMONIC_MOV) {
                        current_fn_ip = instruction.ip;
                        state = IN_FUNCTION;
                    }
                    break;
                case IN_FUNCTION:
                    if (mnemonic == X64_MNEMONIC_RET) {
                        state = SCAN_PROLOGUE;
                        break;
                    }
                    if (x64_instruction_memory_base(&instruction) == X64_REGISTER_RIP &&
                        instruction.rip_address == target) {
                        state = FOUND_REF;
                    }
                    break;
                case FOUND_REF:
                    if (mnemonic == X64_MNEMONIC_RET)
                        return (void *)(uintptr_t)current_fn_ip;
                    break;
                }
            }
        }

        search_start = match_offset + 1;
    }

    return NULL;
}

#elif defined(__aarch64__)

void *section_find_refs(const SectionInfo *section, void *ptr)
{
    uint64_t target;
    const uint32_t *__restrict insns;
    size_t count;
    ScanState state = SCAN_PROLOGUE;
    uint64_t current_fn_ip = 0;

    if (!section || !ptr || section->size < 4)
        return NULL;

    target = (uint64_t)(uintptr_t)ptr;
    insns = (const uint32_t *)(uintptr_t)section->addr;
    count = section->size >> 2;

    for (size_t i = 0; i < count; ++i) {
        uint32_t insn = insns[i];

        if (__builtin_expect(insn == 0xD65F03C0, 0)) {
            if (state == FOUND_REF)
                return (void *)(uintptr_t)current_fn_ip;
            state = SCAN_PROLOGUE;
            continue;
        }

        if (__builtin_expect((insn & 0x9F000000u) == 0x90000000u, 0)) {
            if (state == IN_FUNCTION && i + 1 < count) {
                uint32_t next = insns[i + 1];

                if ((next & 0xFFC00000u) == 0x91000000u) {
                    uint32_t rd = insn & 0x1Fu;
                    if ((next & 0x1Fu) == rd && ((next >> 5) & 0x1Fu) == rd) {
                        int64_t imm21 =
                            ((int64_t)((insn >> 5) & 0x7FFFFu) << 2) |
                            (int64_t)((insn >> 29) & 0x3u);
                        uint64_t page = (section->addr + i * 4u) & ~0xFFFull;
                        uint64_t adrp_val = page + (uint64_t)((imm21 << 43) >> 31);
                        uint32_t imm12 = (next >> 10) & 0xFFFu;
                        uint64_t resolved = adrp_val +
                            (((next >> 22) & 0x3u) == 1u
                                 ? (uint64_t)imm12 << 12
                                 : (uint64_t)imm12);

                        if (resolved == target)
                            state = FOUND_REF;

                        ++i;
                    }
                }
            }
            continue;
        }

        if (__builtin_expect(((insn & 0xFFC07FFFu) == 0xA9807BFDu) ||
                             ((insn & 0xFFC003FFu) == 0xD10003FFu), 0)) {
            if (state == SCAN_PROLOGUE) {
                current_fn_ip = section->addr + i * 4u;
                state = IN_FUNCTION;
            }
            continue;
        }
    }

    return NULL;
}
#else
#error Unsupported architecture
#endif
