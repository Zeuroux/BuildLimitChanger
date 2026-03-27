macro_rules! log_dim_change {
    ($cond:expr, $name:expr, $label:expr, $old:expr, $cfg:expr, $new:expr) => {
        if $cond { log::warn!("{} Dimension Config {} {} not divisible by 16, aligning to {}", $name, $label, $cfg, $new) }
        if $old != $new { log::info!("Changing {} Dimension {}: {} to {}", $name, $label, $old, $new) }
    };
}

macro_rules! aligned {
    ($val:expr, $up:expr) => {{
        let r = $val % 16;
        if r == 0 { $val } else if $up { $val + (16 - r) } else { $val - r }
    }};
}

macro_rules! change_range {
    ($range_addr:expr) => {
        let range_address = $range_addr;
        let range: i32 = std::ptr::read_volatile(range_address);
        const MAX_NAME_LEN: usize = 15;
       let mut name_bytes: [u8; MAX_NAME_LEN] = [0; MAX_NAME_LEN];
        std::ptr::copy_nonoverlapping(
            (range_address as *const u8).add(4),
            name_bytes.as_mut_ptr(),
            MAX_NAME_LEN,
        );

        let end = name_bytes
            .iter()
            .position(|&c| c == 0)
            .unwrap_or(MAX_NAME_LEN);

        let cleaned: Vec<u8> = name_bytes[..end]
            .iter()
            .copied()
            .filter(|b| !b.is_ascii_control())
            .collect();

        let name = String::from_utf8_lossy(&cleaned).to_string();
        use crate::{config, utils::{combine_hex, split_hex}};
        let (max, min) = split_hex(range);
        let (cfg_min, cfg_max) = config::load().get(&name).map(|d| (d.min, d.max)).unwrap_or((min, max));
        let new_min = aligned!(cfg_min, false);
        let new_max = aligned!(cfg_max, true);
        log_dim_change!(cfg_min % 16 != 0, name, "Min", min, cfg_min, new_min);
        log_dim_change!(cfg_max % 16 != 0, name, "Max", max, cfg_max, new_max);
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
