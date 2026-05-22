#include "x64_decoder.h"

#include <string.h>

static const uint8_t k_mrm_1[32] = {
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
    0x00, 0x00, 0x00, 0x00,
    0x08, 0x0A, 0x00, 0x00,
    0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xC3, 0x00,
    0x0F, 0xFF,
    0x00, 0x00,
    0xC0, 0xC0,
};

static const uint8_t k_mrm_0f[32] = {
    0x0F, 0xA0, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF,
    0x00, 0x00, 0xFF, 0xFF, 0x38, 0xF8, 0xFF, 0xFF,
    0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static bool has_mrm_1(uint8_t op)
{
    return ((k_mrm_1[op >> 3] >> (op & 7)) & 1u) != 0;
}

static bool has_mrm_0f(uint8_t op)
{
    return ((k_mrm_0f[op >> 3] >> (op & 7)) & 1u) != 0;
}

static int imm_1(uint8_t op, bool p66, bool rex_w, bool p67)
{
    switch (op) {
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C:
    case 0x6A:
    case 0x6B:
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F:
    case 0x80: case 0x82: case 0x83:
    case 0xA8:
    case 0xB0: case 0xB1: case 0xB2: case 0xB3:
    case 0xB4: case 0xB5: case 0xB6: case 0xB7:
    case 0xC0: case 0xC1:
    case 0xC6:
    case 0xCD:
    case 0xD4: case 0xD5:
    case 0xE0: case 0xE1: case 0xE2: case 0xE3:
    case 0xE4: case 0xE5: case 0xE6: case 0xE7:
    case 0xEB:
        return 1;
    case 0xC2: case 0xCA:
        return 2;
    case 0xC8:
        return 3;
    case 0xE8: case 0xE9:
        return 4;
    case 0x05: case 0x0D: case 0x15: case 0x1D:
    case 0x25: case 0x2D: case 0x35: case 0x3D:
    case 0x68:
    case 0x69:
    case 0x81:
    case 0xA9:
    case 0xC7:
        return p66 ? 2 : 4;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        return rex_w ? 8 : (p66 ? 2 : 4);
    case 0xA0: case 0xA1: case 0xA2: case 0xA3:
        return p67 ? 4 : 8;
    default:
        return 0;
    }
}

static int imm_0f(uint8_t op)
{
    switch (op) {
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0xA4: case 0xAC: case 0xBA:
    case 0xC2: case 0xC4: case 0xC5: case 0xC6:
    case 0x0F:
        return 1;
    default:
        return (op >= 0x80 && op <= 0x8F) ? 4 : 0;
    }
}

static void do_modrm(const uint8_t **cursor, const uint8_t *end,
                     uint64_t ip, const uint8_t *start,
                     bool pfx67, int imm_bytes,
                     bool *rip_rel, uint64_t *rip_addr)
{
    const uint8_t *p = *cursor;
    uint8_t modrm;
    uint8_t mod;
    uint8_t rm;

    if (p >= end)
        return;

    modrm = *p++;
    mod = modrm >> 6;
    rm = modrm & 7u;

    if (mod == 3) {
        *cursor = p;
        return;
    }

    if (!pfx67 && rm == 4) {
        uint8_t sib;
        uint8_t base;

        if (p >= end) {
            *cursor = p;
            return;
        }

        sib = *p++;
        base = sib & 7u;
        if (mod == 0 && base == 5) {
            if (p + 4 <= end)
                p += 4;
        } else if (mod == 1) {
            if (p + 1 <= end)
                p += 1;
        } else if (mod == 2) {
            if (p + 4 <= end)
                p += 4;
        }
    } else if (mod == 0 && rm == 5) {
        int32_t disp;

        if (p + 4 > end) {
            *cursor = p;
            return;
        }

        memcpy(&disp, p, sizeof(disp));
        p += 4;
        if (!pfx67) {
            uint64_t next_ip = ip + (uint64_t)(p - start) + (uint64_t)imm_bytes;
            *rip_rel = true;
            *rip_addr = next_ip + (uint64_t)(int64_t)disp;
        }
    } else {
        if (mod == 1) {
            if (p + 1 <= end)
                p += 1;
        } else if (mod == 2) {
            if (p + 4 <= end)
                p += 4;
        }
    }

    *cursor = p;
}

static int decode_instruction(const uint8_t *start, const uint8_t *end, uint64_t ip,
                              X64Mnemonic *mnemonic, bool *rip_rel, uint64_t *rip_addr)
{
    const uint8_t *p = start;
    bool pfx66 = false;
    bool pfx67 = false;
    uint8_t rex = 0;
    bool rex_w;
    uint8_t op;
    bool esc = false;
    bool esc38 = false;
    bool esc3a = false;
    int imm_bytes;
    bool need_mrm;

    *mnemonic = X64_MNEMONIC_OTHER;
    *rip_rel = false;
    *rip_addr = 0;

    while (p < end) {
        uint8_t b = *p;
        if (b == 0x66) {
            pfx66 = true;
            ++p;
        } else if (b == 0x67) {
            pfx67 = true;
            ++p;
        } else if (b == 0xF0 || b == 0xF2 || b == 0xF3) {
            ++p;
        } else if (b == 0x2E || b == 0x3E || b == 0x26 ||
                   b == 0x64 || b == 0x65 || b == 0x36) {
            ++p;
        } else if ((b & 0xF0u) == 0x40) {
            rex = b;
            ++p;
        } else {
            break;
        }
    }

    if (p >= end)
        return 0;

    rex_w = (rex & 0x08u) != 0;
    op = *p++;

    if (op == 0xC4 || op == 0xC5 || op == 0x62) {
        int hdr = (op == 0xC5) ? 1 : (op == 0xC4) ? 2 : 3;
        uint8_t vmap = 1;
        uint8_t vop;
        int vi = 0;

        if (p + hdr >= end)
            return 0;

        if (op == 0xC4)
            vmap = p[0] & 0x1Fu;
        else if (op == 0x62)
            vmap = p[0] & 0x03u;
        p += hdr;

        if (p >= end)
            return 0;

        vop = *p++;
        if (vmap == 3) {
            vi = 1;
        } else if (vmap == 1) {
            switch (vop) {
            case 0x70: case 0x71: case 0x72: case 0x73:
            case 0xC2: case 0xC4: case 0xC5: case 0xC6:
                vi = 1;
                break;
            default:
                break;
            }
        }

        if (p >= end)
            return 0;

        do_modrm(&p, end, ip, start, pfx67, vi, rip_rel, rip_addr);
        p += vi;
        return (int)(p - start);
    }

    if (op == 0x0F) {
        esc = true;
        if (p >= end)
            return 0;
        op = *p++;
        if (op == 0x38) {
            esc38 = true;
            if (p >= end)
                return 0;
            op = *p++;
        } else if (op == 0x3A) {
            esc3a = true;
            if (p >= end)
                return 0;
            op = *p++;
        }
    }

    if (!esc) {
        if ((op >= 0x50 && op <= 0x57) || op == 0x68 || op == 0x6A) {
            *mnemonic = X64_MNEMONIC_PUSH;
        } else if (op == 0xC3 || op == 0xCB || op == 0xC2 || op == 0xCA) {
            *mnemonic = X64_MNEMONIC_RET;
        } else if ((op >= 0x88 && op <= 0x8C) || op == 0x8E ||
                   (op >= 0xA0 && op <= 0xA3) ||
                   (op >= 0xB0 && op <= 0xBF) ||
                   op == 0xC6 || op == 0xC7) {
            *mnemonic = X64_MNEMONIC_MOV;
        }
    } else if (!esc38 && !esc3a) {
        if (op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF)
            *mnemonic = X64_MNEMONIC_MOV;
    }

    if (esc3a)
        imm_bytes = 1;
    else if (esc38)
        imm_bytes = 0;
    else if (esc)
        imm_bytes = imm_0f(op);
    else
        imm_bytes = imm_1(op, pfx66, rex_w, pfx67);

    need_mrm = (esc3a || esc38) ? true : esc ? has_mrm_0f(op) : has_mrm_1(op);
    if (need_mrm) {
        uint8_t modrm;
        uint8_t reg;

        if (p >= end)
            return 0;

        modrm = *p;
        reg = (modrm >> 3) & 7u;

        if (!esc && op == 0xFF && reg == 6)
            *mnemonic = X64_MNEMONIC_PUSH;

        if (!esc && (op == 0xF6 || op == 0xF7) && (reg == 0 || reg == 1))
            imm_bytes = (op == 0xF6) ? 1 : (pfx66 ? 2 : 4);

        do_modrm(&p, end, ip, start, pfx67, imm_bytes, rip_rel, rip_addr);
    }

    p += imm_bytes;
    if (p > end)
        p = end;

    return (int)(p - start);
}

void x64_decoder_init(X64Decoder *decoder, const uint8_t *data, size_t size, uint64_t ip)
{
    if (!decoder)
        return;

    decoder->data = data;
    decoder->size = size;
    decoder->pos = 0;
    decoder->ip = ip;
}

bool x64_decoder_next(X64Decoder *decoder, X64Instruction *instruction)
{
    int len;

    if (!decoder || !instruction || decoder->pos >= decoder->size)
        return false;

    memset(instruction, 0, sizeof(*instruction));
    instruction->ip = decoder->ip + decoder->pos;
    instruction->mnemonic = X64_MNEMONIC_OTHER;

    len = decode_instruction(decoder->data + decoder->pos,
                             decoder->data + decoder->size,
                             instruction->ip,
                             &instruction->mnemonic,
                             &instruction->rip_relative,
                             &instruction->rip_address);
    if (len <= 0)
        len = 1;

    decoder->pos += (size_t)len;
    return true;
}
