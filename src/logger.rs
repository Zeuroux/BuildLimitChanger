use crate::config::{self, config_path};
use log::{Level, Log, Metadata, Record};
use std::{collections::VecDeque, fs::{File, OpenOptions}, io::Write, sync::{Mutex, OnceLock}, time::{SystemTime, UNIX_EPOCH}};

#[cfg(target_os = "android")]
unsafe extern "C" { fn __android_log_print(prio: i32, tag: *const u8, fmt: *const u8, ...) -> i32; }
#[cfg(target_os = "android")]
macro_rules! platform_print { ($level:expr, $tag:expr, $msg: expr) => { unsafe { __android_log_print(($level as i32 - 7) * -1, std::ffi::CString::new($tag).unwrap().as_ptr() as *const u8, std::ffi::CString::new($msg).unwrap().as_ptr() as *const u8); } }; }
#[cfg(any(target_os = "windows", target_os = "linux"))]
macro_rules! platform_print { ($level:expr, $tag:expr, $msg: expr) => { println!("[{}] [{}]: {}\n\0", $tag, $level, $msg) }; }
pub struct SimpleLogger { pub file: OnceLock<Mutex<File>>, pub buffer: Mutex<VecDeque<(String, String)>>,}
pub static LOGGER: SimpleLogger = SimpleLogger { file: OnceLock::new(), buffer: Mutex::new(VecDeque::new())};

impl Log for SimpleLogger {
    fn enabled(&self, metadata: &Metadata) -> bool { metadata.level() <= Level::Debug }

    fn log(&self, record: &Record) {
        if !self.enabled(record.metadata()) { return; }

        let duration = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
        let timestamp = format!("{:02}:{:02}:{:02}.{:03}", (duration.as_secs() / 3600) % 24, (duration.as_secs() / 60) % 60, duration.as_secs() % 60, duration.subsec_millis());
        let tag = "BuildLimitChanger";
        let msg_less = record.args().to_string();
        let msg = format!("[{timestamp}] [{}] {}\n", record.level(), msg_less);

        platform_print!(record.level(), tag, msg_less.clone());

        if let Some(file_mutex) = self.file.get() {
            if let Some(path) = config::log_path() {
                if !path.exists() {
                    if let Ok(new_file) = OpenOptions::new().create(true).append(true).open(&path) {
                        if let Ok(mut f) = file_mutex.lock() {
                            *f = new_file;
                        }
                    }
                }
                if let Ok(mut f) = file_mutex.lock() {
                    f.write_all(msg.as_bytes()).unwrap_or_else(|e| platform_print!(Level::Error, tag, format!("Log write error: {}", e)))
                }
            }
        } else if let Ok(mut buf) = self.buffer.lock() {
            buf.push_back((msg, msg_less));
        }
        if record.level() == Level::Error {
            panic!("Error: {}", record.args().to_string());
        }
    }
    fn flush(&self) {}
}

pub fn init_log_file() {
    if let Some(path) = config::log_path() {
        path.parent().map(|p| std::fs::create_dir_all(p).ok());
        LOGGER.file.set(Mutex::new(OpenOptions::new().create(true).append(true)
            .open(&path).expect("Failed to open log file"))).expect("Logger file already set");
        if let (Some(fm), Ok(mut buf)) = (LOGGER.file.get(), LOGGER.buffer.lock()) {
            while let Some((msg, msg_less)) = buf.pop_front() {
                let _ = fm.lock().unwrap().write_all(msg.as_bytes());
                platform_print!(Level::Debug, "BuildLimitChanger", msg_less);
            }
        }
        log::info!("\n    Logs: {}\n    Config: {}", path.display(), config_path().unwrap().display());
    }
}