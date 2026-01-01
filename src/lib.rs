#![allow(non_snake_case)]
mod config;
mod hook;
mod logger;
mod utils;

fn find_water_mob_cap_and_fn_starts(data: &[u8]) -> (Option<usize>, Vec<usize>) {
    let mut seen_ret = false;
    let mut possible_fn_starts = Vec::new();
    let mut last_possible_water_mob_cap: Option<usize> = None;
    let mut water_mob_cap: Option<usize> = None;
    let mut closest_distance = usize::MAX;

    #[cfg(target_arch = "aarch64")] {
        const MASKS: [u32; 3] = [0xFFFF_FC1F, 0xFFFF_FFE0, 0xFF00_0000];
        const PATTERNS: [u32; 3] = [0xD65F_0000, 0x52A8_4200, 0xD100_0000];
        const LMAO: u32 = 0xF2E8_4200;
        for inst in data.chunks_exact(4) {
            let addr = inst.as_ptr() as usize;
            let instr = u32::from_le_bytes(inst.try_into().unwrap());
            if (instr & MASKS[0]) == PATTERNS[0] {
                seen_ret = true;
            } else if seen_ret && (instr & MASKS[2]) == PATTERNS[2] {
                possible_fn_starts.push(addr);
                seen_ret = false;
            } else {
                let masked = instr & MASKS[1];
                if !(masked == PATTERNS[1] || masked == LMAO) {
                    continue;
                }
                log::debug!("instr {:X}", instr);
                if let Some(prev) = last_possible_water_mob_cap {
                    let dist = addr.wrapping_sub(prev);
                    if dist < closest_distance {
                        closest_distance = dist;
                        water_mob_cap = Some(addr);
                    }
                }
                last_possible_water_mob_cap = Some(addr);
            }
        }
    }
    #[cfg(any(target_arch = "x86_64", target_arch = "x86"))] {
        use iced_x86::{Decoder, Instruction, Mnemonic};
        #[cfg(target_arch = "x86_64")] const BITNESS: u32 = 64;
        #[cfg(target_arch = "x86")] const BITNESS: u32 = 32;
        let mut decoder = Decoder::new(BITNESS, data, iced_x86::DecoderOptions::NO_INVALID_CHECK);
        decoder.set_ip(data.as_ptr() as u64);
        let mut instruction = Instruction::default();

        const TARGET_IMMEDIATE: u64 = 0x42100000;
        const TARGET_IMMEDIATE2: u64 = 0x41000000;
        let mut lastTarget: u64 = 0;
        while decoder.can_decode() {
            decoder.decode_out(&mut instruction);

            match instruction.mnemonic() {
                Mnemonic::Ret => seen_ret = true,
                Mnemonic::Mov => {
                    #[cfg(all(target_os = "windows", target_arch = "x86_64"))]
                    if seen_ret {
                        possible_fn_starts.push(instruction.ip() as usize);
                        seen_ret = false;
                        continue;
                    }
                    let current = instruction.try_immediate(1).unwrap_or(0);
                    let is_target = matches!(current, TARGET_IMMEDIATE | TARGET_IMMEDIATE2);
                    if is_target && lastTarget != current && !(lastTarget == 0 && current == TARGET_IMMEDIATE2) {
                        lastTarget = current;
                        let addr = instruction.ip() as usize;
                        if let Some(prev) = last_possible_water_mob_cap {
                            let dist = addr.wrapping_sub(prev);
                            if dist < closest_distance {
                                closest_distance = dist;
                                water_mob_cap = Some(addr);
                            }
                        }
                        last_possible_water_mob_cap = Some(addr);
                    }
                },
                #[cfg(any(target_os = "android", target_os = "linux", all(target_os = "windows", target_arch = "x86")))]
                Mnemonic::Push if seen_ret => {
                    possible_fn_starts.push(instruction.ip() as usize);
                    seen_ret = false;
                }
                _ => {}
            }
        }
        if water_mob_cap.is_none() && last_possible_water_mob_cap.is_some() {
            water_mob_cap = Some(last_possible_water_mob_cap.unwrap())
        }
    }

    (water_mob_cap, possible_fn_starts)
}

#[cfg_attr(target_os = "android", no_mangle)]
fn init() {
    let time_start = std::time::Instant::now();
    let mcmap = utils::find_minecraft_text_section().unwrap_or_else(|e|  { panic!("Cannot find Minecraft .text section: {}", e.to_string()) });
    let data = unsafe { std::slice::from_raw_parts(mcmap.start as *const u8, mcmap.size) };
    let (water_mob_cap, possible_fn_starts) = find_water_mob_cap_and_fn_starts(data);
    if water_mob_cap.is_none() { return log::error!("Cannot find the water mob cap"); }
    let Some(function_addr) = utils::find_max_less_than(&possible_fn_starts, water_mob_cap.unwrap()) else {
        return log::error!("Cannot get the function where water mob cap is located");
    };
    log::debug!("Function Offset: 0x{:X}", function_addr);
    log::debug!("{:02X?}", &data[function_addr - mcmap.start..(function_addr - mcmap.start + 50).min(data.len())]);
    hook::setup_hook(function_addr);
    log::info!("Took: {:?}", time_start.elapsed());
}

#[ctor::ctor]
fn main() {
    println!("Starting BuildLimitChanger");
    log::set_logger(&logger::LOGGER).expect("Logger already set");
    log::set_max_level(log::LevelFilter::Debug);
    #[cfg(any(target_os = "linux", target_os = "windows"))] {
        config::init_config(&mut utils::get_config_directory().expect("Failed to get a valid config directory"));
        logger::init_log_file();
        init(); 
    } 
}

#[cfg(target_os = "android")]
#[no_mangle]
pub extern "C" fn mod_init() {
    config::init_config(&mut String::from("/data/data/com.mojang.minecraftpe"));
    logger::init_log_file();
    init();
}

#[cfg(target_os = "android")]
#[no_mangle]
pub extern "system" fn JNI_OnLoad(vm: jni::JavaVM, _: *mut core::ffi::c_void) -> i32 {
    let mut env = vm.get_env().expect("Cannot get reference to the JNIEnv");
    let mut dir = match utils::get_config_directory(&mut env) {
        Some(v) => v,
        None => return jni::sys::JNI_VERSION_1_6,
    };

    config::init_config(&mut dir);
    logger::init_log_file();
    init();
    return jni::sys::JNI_VERSION_1_6;
}