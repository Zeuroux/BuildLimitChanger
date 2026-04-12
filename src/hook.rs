macro_rules! change_range {
    ($range_addr:expr) => {
        let range_address = $range_addr;
        let range: i32 = std::ptr::read_volatile(range_address);

        fn read_c_string(ptr: *const u8) -> Vec<u8> {
            let mut out = Vec::new();
            let mut p = ptr;

            unsafe {
                loop {
                    let b = *p;
                    if b == 0 {
                        break;
                    }

                    if !b.is_ascii_control() {
                        out.push(b);
                    }

                    p = p.add(1);
                }
            }

            out
        }
        #[cfg(any(target_os = "linux", target_os = "android"))]
        let name_bytes = read_c_string((range_address as *const u8).add(5));
        #[cfg(target_os = "windows")]
        let name_bytes = read_c_string((range_address as *const u8).add(4));
        let name = String::from_utf8_lossy(&name_bytes).to_string();
        log::info!("{}", name);
        use crate::{config, utils::{combine_hex, split_hex}};
        let (max, min) = split_hex(range);
        let (new_min, new_max) = config::get(&name, min, max);
        (min != new_min).then(|| log::info!("Changing {} Dimension Min: {} to {}", name, min, new_min));
        (max != new_max).then(|| log::info!("Changing {} Dimension Max: {} to {}", name, max, new_max));
        // let new_min = aligned!(cfg_min, false);
        // let new_max = aligned!(cfg_max, true);
        // log_dim_change!(cfg_min % 16 != 0, name, "Min", min, cfg_min, new_min);
        // log_dim_change!(cfg_max % 16 != 0, name, "Max", max, cfg_max, new_max);
        *range_address = combine_hex(new_max, new_min);
    };
}

bhook::hook_fn! {
    fn hook(a: *mut std::ffi::c_void, b: *mut std::ffi::c_void) -> i64 = {
        #[cfg(target_os = "windows")]
        change_range!((b as *mut u8).offset(0x54) as *mut i32);
        #[cfg(any(target_os = "linux", target_os = "android"))]
        change_range!((b as *mut u8).offset(0x64) as *mut i32);
        call_original(a, b)
    }
}

pub fn setup_hook(function_addr: usize) {
    unsafe { hook::hook_address(function_addr as *mut u8) };
    log::debug!("Hooked function at 0x{:X}", function_addr);
}
