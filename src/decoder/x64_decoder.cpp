// AI GENERATED SLOP
#include "x64_decoder.hpp"
#include <cstring>

namespace x64_decoder {

// ── internals ────────────────────────────────────────────────────────────────
namespace detail {

// Bit (op & 7) of byte [op >> 3] is set when opcode has a ModRM byte.
// 1-byte opcodes, 64-bit mode.
static constexpr uint8_t kMrm1[32] = {
//  +0    +8    +10   +18   +20   +28   +30   +38   (each byte = 8 opcodes)
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 00–3F  ADD/OR/ADC/SBB/AND/SUB/XOR/CMP
    0x00, 0x00, 0x00, 0x00,                           // 40–5F  REX / PUSH / POP
    0x08, 0x0A, 0x00, 0x00,                           // 60–7F  (63=MOVSXD, 69/6B=IMUL, Jcc)
    0xFF, 0xFF,                                       // 80–8F  group1 / TEST / XCHG / MOV / LEA
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              // 90–BF  NOP / string / MOV imm
    0xC3, 0x00,                                       // C0–CF  shifts / RET / VEX / MOV r/m,imm
    0x0F, 0xFF,                                       // D0–DF  shifts / x87
    0x00, 0x00,                                       // E0–EF  LOOP / CALL / JMP
    0xC0, 0xC0,                                       // F0–FF  group3(F6/F7) / group4/5(FE/FF)
};

// Same for 0F-prefixed opcodes.
static constexpr uint8_t kMrm0F[32] = {
    0x0F, 0xA0, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, // 00–3F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, // 40–7F  (77=EMMS has no ModRM)
    0x00, 0x00, 0xFF, 0xFF, 0x38, 0xF8, 0xFF, 0xFF, // 80–BF  (80–8F=Jcc no ModRM)
    0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // C0–FF  (C8–CF=BSWAP no ModRM)
};

inline bool has_mrm1 (uint8_t op) { return (kMrm1 [op >> 3] >> (op & 7)) & 1; }
inline bool has_mrm0F(uint8_t op) { return (kMrm0F[op >> 3] >> (op & 7)) & 1; }

// Immediate byte count for 1-byte opcodes (F6/F7 TEST adjusted later from ModRM.reg).
static int imm1(uint8_t op, bool p66, bool rex_w, bool p67)
{
    switch (op) {
    // ── always 1 byte ────────────────────────────────────────────────────────
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C: // AL,imm8 (ADD..CMP)
    case 0x6A:                                   // PUSH imm8
    case 0x6B:                                   // IMUL r,r/m,imm8
    case 0x70: case 0x71: case 0x72: case 0x73: // Jcc rel8
    case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F:
    case 0x80: case 0x82: case 0x83:             // group1 /imm8
    case 0xA8:                                   // TEST AL,imm8
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: // MOV r8,imm8
    case 0xB4: case 0xB5: case 0xB6: case 0xB7:
    case 0xC0: case 0xC1:                        // shift r/m8,imm8
    case 0xC6:                                   // MOV r/m8,imm8
    case 0xCD:                                   // INT imm8
    case 0xD4: case 0xD5:                        // AAM/AAD (invalid in 64b, still skip)
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: // LOOP / JECXZ
    case 0xE4: case 0xE5: case 0xE6: case 0xE7: // IN/OUT imm8
    case 0xEB:                                   // JMP rel8
        return 1;
    // ── always 2 bytes ───────────────────────────────────────────────────────
    case 0xC2: case 0xCA:                        // RET/RETF imm16
        return 2;
    // ── 3 bytes (ENTER imm16,imm8) ───────────────────────────────────────────
    case 0xC8:
        return 3;
    // ── CALL/JMP rel32 ───────────────────────────────────────────────────────
    case 0xE8: case 0xE9:
        return 4;
    // ── immZ: 2 with 66-prefix, else 4 ──────────────────────────────────────
    case 0x05: case 0x0D: case 0x15: case 0x1D:
    case 0x25: case 0x2D: case 0x35: case 0x3D: // rAX,immZ (ADD..CMP)
    case 0x68:                                   // PUSH imm32
    case 0x69:                                   // IMUL r,r/m,immZ
    case 0x81:                                   // group1 r/m,immZ
    case 0xA9:                                   // TEST rAX,immZ
    case 0xC7:                                   // MOV r/m,immZ
        return p66 ? 2 : 4;
    // ── MOV r,immQ: 8(REX.W) / 4 / 2(66) ───────────────────────────────────
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        return rex_w ? 8 : (p66 ? 2 : 4);
    // ── MOV moffs: 8-byte addr (4 with 67-prefix) ────────────────────────────
    case 0xA0: case 0xA1: case 0xA2: case 0xA3:
        return p67 ? 4 : 8;
    default:
        return 0;
    }
}

// Immediate byte count for 0F-prefixed opcodes.
static int imm0F(uint8_t op, bool /*p66*/)
{
    switch (op) {
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0xA4: case 0xAC: case 0xBA:
    case 0xC2: case 0xC4: case 0xC5: case 0xC6:
    case 0x0F:  // 3DNow! opcode suffix
        return 1;
    default:    // Jcc rel32 (0F 80–8F)
        return (op >= 0x80 && op <= 0x8F) ? 4 : 0;
    }
}

// SIB/disp helper — advances p, sets rip_rel/rip_addr if applicable.
static void do_modrm(const uint8_t*& p, const uint8_t* end,
                     uint64_t ip, const uint8_t* start,
                     bool pfx67, int imm_bytes,
                     bool& rip_rel, uint64_t& rip_addr)
{
    uint8_t modrm = *p++;
    uint8_t mod   = modrm >> 6;
    uint8_t rm    = modrm & 7;

    if (mod == 3) return;               // register operand, no memory

    if (!pfx67 && rm == 4) {            // SIB byte
        if (p >= end) return;
        uint8_t sib  = *p++;
        uint8_t base = sib & 7;
        if      (mod == 0 && base == 5) p += 4;
        else if (mod == 1)              p += 1;
        else if (mod == 2)              p += 4;
    } else if (mod == 0 && rm == 5) {   // RIP-relative disp32
        if (p + 4 > end) return;
        int32_t disp; memcpy(&disp, p, 4); p += 4;
        if (!pfx67) {
            uint64_t next_ip = ip + (p - start) + imm_bytes;
            rip_rel  = true;
            rip_addr = next_ip + (uint64_t)(int64_t)disp;
        }
    } else {
        if      (mod == 1) p += 1;
        else if (mod == 2) p += 4;
    }
}

// Returns instruction length (>0) or 0 on error.
static int decode(const uint8_t* start, const uint8_t* end, uint64_t ip,
                  Mnemonic& mnem, bool& rip_rel, uint64_t& rip_addr)
{
    const uint8_t* p = start;
    mnem    = Mnemonic::OTHER;
    rip_rel = false;

    // ── 1. Legacy prefixes ────────────────────────────────────────────────────
    bool    pfx66 = false, pfx67 = false;
    uint8_t rex   = 0;
    while (p < end) {
        uint8_t b = *p;
        if      (b == 0x66)                          { pfx66 = true; ++p; }
        else if (b == 0x67)                          { pfx67 = true; ++p; }
        else if (b == 0xF0 || b == 0xF2 || b == 0xF3)              { ++p; }
        else if (b == 0x2E || b == 0x3E || b == 0x26 ||
                 b == 0x64 || b == 0x65 || b == 0x36)               { ++p; }
        else if ((b & 0xF0) == 0x40)                 { rex = b; ++p; }
        else break;
    }
    if (p >= end) return 0;

    const bool rex_w = (rex & 0x08) != 0;
    uint8_t op = *p++;

    // ── 2. VEX (C4/C5) / EVEX (62) ───────────────────────────────────────────
    if (op == 0xC4 || op == 0xC5 || op == 0x62) {
        int    hdr  = (op == 0xC5) ? 1 : (op == 0xC4) ? 2 : 3;
        if (p + hdr >= end) return 0;

        uint8_t vmap = 1;
        if      (op == 0xC4)  vmap = p[0] & 0x1F;
        else if (op == 0x62)  vmap = p[0] & 0x03;
        p += hdr;

        if (p >= end) return 0;
        uint8_t vop = *p++;                             // actual opcode

        // check for imm8 before we record rip_addr
        int vi = 0;
        if (vmap == 3) vi = 1;  // 0F 3A map always has imm8
        else if (vmap == 1) {
            switch (vop) {
            case 0x70: case 0x71: case 0x72: case 0x73:
            case 0xC2: case 0xC4: case 0xC5: case 0xC6: vi = 1; break;
            }
        }

        if (p >= end) return 0;
        do_modrm(p, end, ip, start, pfx67, vi, rip_rel, rip_addr);
        p += vi;
        return (int)(p - start);
    }

    // ── 3. 0F escape ──────────────────────────────────────────────────────────
    bool esc = false, esc38 = false, esc3a = false;
    if (op == 0x0F) {
        esc = true;
        if (p >= end) return 0;
        op = *p++;
        if      (op == 0x38) { esc38 = true; if (p >= end) return 0; op = *p++; }
        else if (op == 0x3A) { esc3a = true; if (p >= end) return 0; op = *p++; }
    }

    // ── 4. Mnemonic (1-byte opcodes only) ─────────────────────────────────────
    if (!esc) {
        if ((op >= 0x50 && op <= 0x57) || op == 0x68 || op == 0x6A)
            mnem = Mnemonic::PUSH;
        else if (op == 0xC3 || op == 0xCB || op == 0xC2 || op == 0xCA)
            mnem = Mnemonic::RET;
        // MOV r/m,r | r,r/m (88–8B); MOV r/m,Sreg | Sreg,r/m (8C,8E);
        // MOV AL/rAX,moffs | moffs,AL/rAX (A0–A3);
        // MOV r8,imm8 (B0–B7); MOV r,immQ (B8–BF);
        // MOV r/m8,imm8 (C6); MOV r/m,immZ (C7)
        else if ((op >= 0x88 && op <= 0x8C) || op == 0x8E ||
                 (op >= 0xA0 && op <= 0xA3) ||
                 (op >= 0xB0 && op <= 0xBF) ||
                 op == 0xC6 || op == 0xC7)
            mnem = Mnemonic::MOV;
    } else if (esc && !esc38 && !esc3a) {
        // MOVZX (0F B6/B7), MOVSX (0F BE/BF)
        // Note: MOV CRn/DRn (0F 20–23) deliberately omitted — ring-0 only,
        // and 0x20–0x23 are AND opcodes in plain encoding, making them
        // dangerous to tag if the decoder ever desyncs across a 0F byte.
        if (op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF)
            mnem = Mnemonic::MOV;
    }

    // ── 5. Immediate size ──────────────────────────────────────────────────────
    int imm_bytes;
    if      (esc3a)  imm_bytes = 1;
    else if (esc38)  imm_bytes = 0;
    else if (esc)    imm_bytes = imm0F(op, pfx66);
    else             imm_bytes = imm1 (op, pfx66, rex_w, pfx67);

    // ── 6. ModRM + SIB + displacement ─────────────────────────────────────────
    bool need_mrm = (esc3a || esc38) ? true
                  : esc              ? has_mrm0F(op)
                  :                    has_mrm1 (op);

    if (need_mrm) {
        if (p >= end) return 0;

        // Peek at ModRM for special cases before consuming
        uint8_t modrm = *p;
        uint8_t reg   = (modrm >> 3) & 7;

        // FF /6 → PUSH r/m
        if (!esc && op == 0xFF && reg == 6)  mnem = Mnemonic::PUSH;

        // F6/F7 TEST: imm size depends on reg field
        if (!esc && (op == 0xF6 || op == 0xF7) && (reg == 0 || reg == 1))
            imm_bytes = (op == 0xF6) ? 1 : (pfx66 ? 2 : 4);

        do_modrm(p, end, ip, start, pfx67, imm_bytes, rip_rel, rip_addr);
    }

    // ── 7. Consume immediate ──────────────────────────────────────────────────
    p += imm_bytes;
    if (p > end) p = end;
    return (int)(p - start);
}

} // namespace detail

bool Decoder::decode_next(Instruction& instr) {
    if (_pos >= _size) return false;

    instr         = Instruction{};
    instr._ip     = _ip + _pos;

    int len = detail::decode(
        _data + _pos, _data + _size,
        instr._ip,
        instr._mn, instr._rip, instr._rip_addr
    );

    if (len <= 0) len = 1;
    _pos += len;

    return true;
}

} // namespace x64_decoder