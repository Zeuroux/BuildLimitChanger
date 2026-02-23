#![allow(non_snake_case)]
mod config;
mod hook;
mod logger;
mod utils;
mod sections;
mod searcher;

use sections::{get_sections};
use searcher::{get_string_addr, find_fn_string_refs};

#[cfg_attr(target_os = "android", unsafe(no_mangle))]
fn init() {
    let start = std::time::Instant::now();
    let sections = get_sections().expect("Failed to get module sections");
    const STR_VAL: &str = "A dimension task group";

    let str_addr = sections.iter()
        .filter(|s| matches!(s.name.as_str(), ".rodata" | ".rdata" | ".data"))
        .find_map(|s| get_string_addr(s.as_bytes(), STR_VAL, s.addr as u64));

    if let Some(addr) = str_addr {
        log::debug!("Found {} at 0x{:X}", STR_VAL, addr);
        if let Some(func) = sections.iter().find(|s| s.name == ".text").and_then(|s| find_fn_string_refs(s.as_bytes(), s.addr as u64, addr).ok()) {
            hook::setup_hook((func as u64).try_into().unwrap());
        }
    } else {
        log::error!("Failed to find string");
    }

    log::info!("Took: {:?}", start.elapsed());
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
#[unsafe(no_mangle)]
pub extern "C" fn mod_init() {
    config::init_config(&mut String::from("/data/data/com.mojang.minecraftpe"));
    logger::init_log_file();
    init();
}

#[cfg(target_os = "android")]
#[unsafe(no_mangle)]
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