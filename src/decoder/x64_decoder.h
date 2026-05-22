#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum X64Mnemonic {
    X64_MNEMONIC_OTHER = 0,
    X64_MNEMONIC_PUSH,
    X64_MNEMONIC_RET,
    X64_MNEMONIC_MOV
} X64Mnemonic;

typedef enum X64Register {
    X64_REGISTER_NONE = 0,
    X64_REGISTER_RIP
} X64Register;

typedef struct X64Instruction {
    uint64_t ip;
    X64Mnemonic mnemonic;
    bool rip_relative;
    uint64_t rip_address;
} X64Instruction;

typedef struct X64Decoder {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint64_t ip;
} X64Decoder;

void x64_decoder_init(X64Decoder *decoder, const uint8_t *data, size_t size, uint64_t ip);
bool x64_decoder_next(X64Decoder *decoder, X64Instruction *instruction);

static inline X64Register x64_instruction_memory_base(const X64Instruction *instruction)
{
    return instruction && instruction->rip_relative ? X64_REGISTER_RIP : X64_REGISTER_NONE;
}
