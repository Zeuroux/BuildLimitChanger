#pragma once
// AI GENERATED SLOP

#include <cstdint>
#include <span>

namespace x64_decoder {

// ── public API ───────────────────────────────────────────────────────────────

enum class Mnemonic : uint8_t { OTHER, PUSH, RET, MOV };
enum class Register  : uint8_t { NONE,  RIP  };

class Instruction {
    friend class Decoder;
    uint64_t _ip{};
    Mnemonic _mn{ Mnemonic::OTHER };
    bool     _rip{};
    uint64_t _rip_addr{};
public:
    uint64_t ip()                    const { return _ip; }
    Mnemonic mnemonic()              const { return _mn; }
    Register memory_base()           const { return _rip ? Register::RIP : Register::NONE; }
    uint64_t ip_rel_memory_address() const { return _rip_addr; }
};

// ── Decoder ───────────────────────────────────────────────────────────────────

class Decoder {
    const uint8_t* _data;
    size_t         _size;
    size_t         _pos{};
    uint64_t       _ip;
public:
    Decoder(std::span<const uint8_t> data, uint64_t ip)
        : _data(data.data()), _size(data.size()), _ip(ip) {}

    bool decode_next(Instruction& instr);
};

} // namespace x64_decoder