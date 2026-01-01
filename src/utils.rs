use std::error::Error;

pub fn combine_hex(max: i16, min: i16) -> i32 { 
    ((max as i32) << 16) | (min as u16 as i32)
}

pub fn split_hex(combined: i32) -> (i16, i16) {
    ((combined >> 16) as i16, (combined & 0xFFFF) as i16)
}

pub fn find_max_less_than(data: &[usize], target: usize) -> Option<usize> {
    let mut low = 0;
    let mut high = data.len();

    while low < high {
        let mid = low + ((high - low) >> 1);
        let mid_val = unsafe { *data.get_unchecked(mid) };
        if mid_val < target { low = mid + 1; } else { high = mid; }
    }

    if low == 0 { None } else { Some(unsafe { *data.get_unchecked(low - 1) }) }
}

pub struct TextMapRange { pub start: usize, pub size: usize }

#[cfg_attr(target_os = "android", no_mangle)]
pub fn get_config_directory(#[cfg(target_os = "android")] env: &mut jni::JNIEnv) -> Option<String> {
    #[cfg(target_os = "linux")]
    { std::env::current_exe().ok().and_then(|path| path.parent().map(|p| p.to_string_lossy().to_string())) }
    #[cfg(target_os = "windows")] { 
        windows::ApplicationModel::Package::Current().ok()
            .and_then(|_| { 
                windows::Storage::ApplicationData::Current().ok()?.RoamingFolder().ok()?.Path().ok()
            })
            .map(|p| p.to_string_lossy().to_owned())
            .or_else(|| {
                std::env::current_exe().ok()?.parent()?.to_str().map(String::from) 
            }) 
    }
    
    #[cfg(target_os = "android")]
    { get_global_context(env).and_then(|ctx| {get_games_directory(env).or_else(|| get_app_external_files_dir(env, ctx.as_obj()))}) }
}

#[cfg(any(target_os = "android", target_os = "linux"))]
fn find_text_section_for_target(target: &str) -> Result<TextMapRange, Box<dyn Error>> {
    use libc::c_void;
    use std::ffi::CString;
    use std::path::Path;

    let is_executable = {
        let path = Path::new(target);
        path.exists() || (!target.ends_with(".so") && !target.contains('/'))
    };

    struct Ctx { handle: *mut c_void, target_name: String, is_exe: bool, range: (*mut u8, usize) }

    extern "C" fn callback(info: *mut libc::dl_phdr_info, _: libc::size_t, data: *mut c_void) -> libc::c_int {
        unsafe {
            let ctx = &mut *(data as *mut Ctx);
            let info = &*info;
            
            let matched = if ctx.is_exe {
                is_main_executable(info) || is_matching_name(info, &ctx.target_name)
            } else {
                is_matching_handle(info, ctx.handle)
            };
            
            if !matched { return 0; }
            
            for i in 0..info.dlpi_phnum {
                let phdr = &*info.dlpi_phdr.add(i as usize);
                if phdr.p_type == libc::PT_LOAD && phdr.p_flags & 1 != 0 {
                    ctx.range = (
                        (info.dlpi_addr as *mut u8).add(phdr.p_vaddr as usize), 
                        phdr.p_memsz as usize
                    );
                    break;
                }
            }
            1
        }
    }

    unsafe fn is_main_executable(info: &libc::dl_phdr_info) -> bool {
        info.dlpi_name.is_null() || *(info.dlpi_name as *const u8) == 0
    }

    unsafe fn is_matching_name(info: &libc::dl_phdr_info, target_name: &str) -> bool {
        if info.dlpi_name.is_null() { return false; }
        
        if let Ok(name) = std::ffi::CStr::from_ptr(info.dlpi_name).to_str() {
            name == target_name || 
            Path::new(name).file_name().and_then(|n| n.to_str()) == Some(target_name)
        } else { false }
    }

    unsafe fn is_matching_handle(info: &libc::dl_phdr_info, target_handle: *mut c_void) -> bool {
        if info.dlpi_name.is_null() { return false; }
        
        let h = libc::dlopen(info.dlpi_name, libc::RTLD_NOLOAD);
        let matched = !h.is_null() && h == target_handle;
        if !h.is_null() { libc::dlclose(h); }
        matched
    }

    unsafe {
        let handle = if is_executable { std::ptr::null_mut() } else {
            let target_cstr = CString::new(target)?;
            let h = libc::dlopen(target_cstr.as_ptr(), libc::RTLD_LAZY);
            if h.is_null() { return Err(format!("Cannot find library: {}", target).into()); }
            h
        };
        
        let mut ctx = Ctx { handle, target_name: target.to_string(), is_exe: is_executable, range: (std::ptr::null_mut(), 0) };
        
        libc::dl_iterate_phdr(Some(callback), &mut ctx as *mut _ as *mut c_void);
        
        if !handle.is_null() { libc::dlclose(handle); }
        
        if ctx.range.0.is_null() || ctx.range.1 == 0 {
            return Err(format!("Cannot find executable text section for: {}", target).into());
        }
        
        Ok(TextMapRange { start: ctx.range.0 as usize, size: ctx.range.1 })
    }
}

pub fn find_minecraft_text_section() -> Result<TextMapRange, Box<dyn Error>> {
    #[cfg(target_os = "linux")] {
        let exe = std::env::current_exe()?.to_string_lossy().into_owned();
        find_text_section_for_target(&exe).map_err(|_| format!("Can't find executable text section for {exe}").into())
    }
    #[cfg(target_os = "android")] { find_text_section_for_target("libminecraftpe.so").map_err(|_| "Can't find text section for libminecraftpe.so".into()) }
    #[cfg(target_os = "windows")] unsafe {
        use windows_sys::Win32::System::{LibraryLoader::GetModuleHandleW, ProcessStatus::{GetModuleInformation, MODULEINFO}, Threading::GetCurrentProcess};
        let h_module = GetModuleHandleW(std::ptr::null());
        if h_module == 0 { return Err("Failed to get module handle for main executable".into()); }

        let mut mod_info = std::mem::zeroed::<MODULEINFO>();
        if GetModuleInformation(GetCurrentProcess(), h_module, &mut mod_info, std::mem::size_of::<MODULEINFO>() as u32) == 0 {
            return Err("GetModuleInformation failed".into());
        }

        let base_addr = mod_info.lpBaseOfDll as usize;
        let image_slice = std::slice::from_raw_parts(base_addr as *const u8, mod_info.SizeOfImage as usize);
        let text_section = pelite::PeView::from_bytes(image_slice)?.section_headers().iter().find(|s| s.Name.starts_with(b".text")).ok_or(".text section not found")?;

        let text_addr = base_addr + text_section.VirtualAddress as usize;
        let text_size = text_section.VirtualSize as usize;

        log::debug!("Minecraft.Windows.exe .text: addr = 0x{:x}, size = 0x{:x}", text_addr, text_size);

        Ok(TextMapRange { start: text_addr, size: text_size })
    }
}

#[cfg(target_os = "android")]
pub use android_specific::*;

#[cfg(target_os = "android")]
mod android_specific {
    use jni::{objects::{GlobalRef, JObject, JString}, JNIEnv};

    pub fn get_games_directory(env: &mut JNIEnv) -> Option<String> {
        let env_class = env.find_class("android/os/Environment").ok()?;
        let storage_dir = env
            .call_static_method(env_class, "getExternalStorageDirectory", "()Ljava/io/File;", &[])
            .ok()?.l().ok()?;
        
        let mut result = get_absolute_path_from_file(env, storage_dir)?;
        result.push_str("/games");
        Some(result)
    }

    pub fn get_app_external_files_dir(env: &mut JNIEnv, context: &JObject) -> Option<String> {
        let file_obj = env
            .call_method(context, "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;", &[(&JObject::null()).into()])
            .ok()?.l().ok()?;
        get_absolute_path_from_file(env, file_obj)
    }

    pub fn get_global_context(env: &mut JNIEnv) -> Option<GlobalRef> {
        let activity_thread_class = env.find_class("android/app/ActivityThread").ok()?;
        let at_instance = env
            .call_static_method(activity_thread_class, "currentActivityThread", "()Landroid/app/ActivityThread;", &[])
            .ok()?.l().ok()?;
        let context = env
            .call_method(at_instance, "getApplication", "()Landroid/app/Application;", &[])
            .ok()?.l().ok()?;
        
        if env.exception_check().unwrap_or(false) {
            let _ = env.exception_clear();
            return None;
        }
        env.new_global_ref(context).ok()
    }

    fn get_absolute_path_from_file(env: &mut JNIEnv, file_obj: JObject) -> Option<String> {
        let abs_path = env
            .call_method(file_obj, "getAbsolutePath", "()Ljava/lang/String;", &[])
            .ok()?.l().ok()?;
        env.get_string(&JString::from(abs_path)).ok().map(|s| s.into())
    }
}