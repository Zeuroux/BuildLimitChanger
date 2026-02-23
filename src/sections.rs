use std::error::Error;

pub struct SectionInfo {
    pub name: String,
    pub addr: usize,
    pub size: usize,
}

impl SectionInfo {
    pub fn as_bytes(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.addr as *const u8, self.size) }
    }
}

#[cfg(any(target_os = "android", target_os = "linux"))]
fn get_sections_for_target(target: &str) -> Result<Vec<SectionInfo>, Box<dyn Error>> {
    use libc::c_void;
    use std::path::Path;
    use std::ffi::CString;

    let is_executable = {
        let path = Path::new(target);
        path.exists() || (!target.ends_with(".so") && !target.contains('/'))
    };

    struct Ctx {
        handle: *mut c_void,
        target_name: String,
        is_exe: bool,
        base_addr: usize,
        file_path: String,
    }

    extern "C" fn callback(info: *mut libc::dl_phdr_info, _: libc::size_t, data: *mut c_void) -> libc::c_int {
        unsafe {
            let ctx = &mut *(data as *mut Ctx);
            let info = &*info;

            let matched = if ctx.is_exe {
                info.dlpi_name.is_null() || *(info.dlpi_name as *const u8) == 0
                    || is_matching_name(info, &ctx.target_name)
            } else {
                is_matching_handle(info, ctx.handle)
            };

            if !matched { return 0; }

            ctx.base_addr = info.dlpi_addr as usize;

            if !info.dlpi_name.is_null() {
                if let Ok(path) = std::ffi::CStr::from_ptr(info.dlpi_name).to_str() {
                    if !path.is_empty() {
                        ctx.file_path = path.to_string();
                    }
                }
            }
            1
        }
    }

    unsafe fn is_matching_name(info: &libc::dl_phdr_info, target_name: &str) -> bool {
        !info.dlpi_name.is_null() && unsafe { std::ffi::CStr::from_ptr(info.dlpi_name)
            .to_str()
            .ok()
            .map(|name| name == target_name || 
                Path::new(name).file_name().and_then(|n| n.to_str()) == Some(target_name))
            .unwrap_or(false) }
    }

    unsafe fn is_matching_handle(info: &libc::dl_phdr_info, target_handle: *mut c_void) -> bool {
        if info.dlpi_name.is_null() { return false; }
        let h = unsafe { libc::dlopen(info.dlpi_name, libc::RTLD_NOLOAD) };
        let matched = !h.is_null() && h == target_handle;
        if !h.is_null() { unsafe { libc::dlclose(h) }; }
        matched
    }

    unsafe {
        let handle = if is_executable {
            std::ptr::null_mut()
        } else {
            let target_cstr = CString::new(target)?;
            let h = libc::dlopen(target_cstr.as_ptr(), libc::RTLD_LAZY);
            if h.is_null() { return Err(format!("Cannot get library: {}", target).into()); }
            h
        };

        let mut ctx = Ctx {
            handle,
            target_name: target.to_string(),
            is_exe: is_executable,
            base_addr: 0,
            file_path: String::new(),
        };

        libc::dl_iterate_phdr(Some(callback), &mut ctx as *mut _ as *mut c_void);

        if !handle.is_null() { libc::dlclose(handle); }

        let file_path = if ctx.file_path.is_empty() {
            if is_executable {
                std::env::current_exe()?.to_string_lossy().into_owned()
            } else {
                target.to_string()
            }
        } else {
            ctx.file_path
        };

        parse_elf_sections(&file_path, ctx.base_addr)
    }
}

#[cfg(any(target_os = "android", target_os = "linux"))]
fn parse_elf_sections(path: &str, base_addr: usize) -> Result<Vec<SectionInfo>, Box<dyn Error>> {
    use elf::ElfBytes;
    use elf::endian::AnyEndian;
    use std::fs;

    let buffer = fs::read(path)?;
    let elf = ElfBytes::<AnyEndian>::minimal_parse(&buffer)?;

    let (shdrs_table, strtab) = elf.section_headers_with_strtab()?;
    let shdrs = shdrs_table.ok_or("No section headers")?;
    let strtab = strtab.ok_or("No string table")?;

    let mut sections = Vec::with_capacity(shdrs.len());

    for sh in shdrs {
        let name = strtab.get(sh.sh_name as usize)?;
        sections.push(SectionInfo {
            name: name.to_string(),
            addr: base_addr + sh.sh_offset as usize,
            size: sh.sh_size as usize,
        });
    }

    Ok(sections)
}

pub fn get_sections() -> Result<Vec<SectionInfo>, Box<dyn Error>> {
    #[cfg(any(target_os = "android", target_os = "linux"))]
    {
        #[cfg(target_os = "linux")]
        let bin = std::env::current_exe()?.to_string_lossy().into_owned();
        #[cfg(target_os = "android")]
        let bin = "libminecraftpe.so";
        get_sections_for_target(&bin)
            .map_err(|e| format!("Can't get sections for {bin}: {e}").into())
    }
    #[cfg(target_os = "windows")]
    unsafe {
        use windows_sys::Win32::System::{
            LibraryLoader::GetModuleHandleW,
            ProcessStatus::{GetModuleInformation, MODULEINFO},
            Threading::GetCurrentProcess,
        };

        let h_module = GetModuleHandleW(std::ptr::null());
        if h_module == 0 {
            return Err("Failed to get module handle for main executable".into());
        }

        let mut mod_info = std::mem::zeroed::<MODULEINFO>();
        if GetModuleInformation(
            GetCurrentProcess(),
            h_module,
            &mut mod_info,
            std::mem::size_of::<MODULEINFO>() as u32,
        ) == 0
        {
            return Err("GetModuleInformation failed".into());
        }

        let base_addr = mod_info.lpBaseOfDll as usize;
        let image_slice = std::slice::from_raw_parts(
            base_addr as *const u8,
            mod_info.SizeOfImage as usize,
        );
        let pe_view = pelite::PeView::from_bytes(image_slice)?;

        Ok(pe_view.section_headers().iter().map(|s| {
            let name = std::str::from_utf8(&s.Name)
                .unwrap_or("")
                .trim_end_matches('\0')
                .to_string();

            SectionInfo {
                name,
                addr: base_addr + s.VirtualAddress as usize,
                size: s.VirtualSize as usize,
            }
        }).collect())
    }
}