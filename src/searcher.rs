#[cfg(target_arch = "aarch64")]
pub fn find_fn_string_refs(code: &[u8], base_ip: u64, target: u64) -> Result<u64, &'static str> {
    let (mut offset, len) = (0, code.len());
    let (mut current_fn, mut found_target) = (None, false);
    let mut last_adrp: Option<(u8, u64)> = None;

    while offset + 4 <= len {
        let insn = unsafe { (code.as_ptr().add(offset) as *const u32).read_unaligned().to_le() };
        let ip = base_ip + offset as u64;

        if insn == 0xD65F03C0 {
            if found_target { if let Some(f) = current_fn { return Ok(f); } }
            current_fn = None; found_target = false; last_adrp = None;
        } else if (insn & 0x9F000000) == 0x90000000 {
            let imm = ((((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 3)) as i64;
            last_adrp = Some(((insn & 0x1F) as u8, (ip & !0xFFF).wrapping_add(((imm << 43) >> 31) as u64)));
        } else if (insn & 0xFFC00000) == 0x91000000 {
            if let Some((rd, base)) = last_adrp {
                if (insn & 0x1F) as u8 == rd && ((insn >> 5) & 0x1F) as u8 == rd {
                    let imm = (insn >> 10) & 0xFFF;
                    if base.wrapping_add((if ((insn >> 22) & 3) == 1 { imm << 12 } else { imm }) as u64) == target { found_target = true; }
                }
            }
            last_adrp = None;
        } else if (insn & 0xFFC07FFF) == 0xA9807BFD || (insn & 0xFFC003FF) == 0xD10003FF {
            if current_fn.is_none() { current_fn = Some(ip); found_target = false; }
            last_adrp = None;
        } else { last_adrp = None; }
        offset += 4;
    }
    Err("Failed to find references")
}


#[cfg(any(target_arch = "x86_64", target_arch = "x86"))]
pub fn find_fn_string_refs(code: &[u8], base_ip: u64, target: u64) -> Result<u64, &'static str> {
    use iced_x86::{Decoder, DecoderOptions, Instruction, Mnemonic};

    #[cfg(target_arch = "x86_64")]
    const BITNESS: u32 = 64;
    #[cfg(target_arch = "x86")]
    const BITNESS: u32 = 32;

    let mut decoder = Decoder::with_ip(BITNESS, code, base_ip, DecoderOptions::NO_INVALID_CHECK);
    let mut instr = Instruction::default();
    
    let mut current_fn_ip: u64 = 0;
    let mut state = 0;

    while decoder.can_decode() {
        decoder.decode_out(&mut instr);
        let mnemonic = instr.mnemonic();

        if state == 0 {
            if mnemonic == Mnemonic::Push && instr.op_count() == 1 {
                current_fn_ip = instr.ip();
                state = 1;
            }
        } else if state == 1 {
            if mnemonic == Mnemonic::Ret {
                state = 0;
            } else if instr.memory_displacement64() == target {
                state = 2;
            }
        } else {
            if mnemonic == Mnemonic::Ret {
                return Ok(current_fn_ip);
            }
        }
    }

    Err("Failed to find references")
}

pub fn get_string_addr(data: &[u8], target: &'static str, base_va: u64) -> Option<u64> {
    use memchr::memmem;
    let mut target_with_null = Vec::with_capacity(target.len() + 1);
    target_with_null.extend_from_slice(target.as_bytes());
    target_with_null.push(0x00);
    for pos in memmem::find_iter(data, &target_with_null) {
        return  Some(base_va + pos as u64);
    }
    return None;
}