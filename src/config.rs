use indexmap::{IndexMap, map::Entry};
use serde::{Deserialize, Serialize};
use std::{fs, path::{Path, PathBuf}, sync::OnceLock};

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct HeightRange { pub min: i16, pub max: i16 }

type HeightRangeMap = IndexMap<String, HeightRange>;

trait Create {
    fn create() -> Self;
}

impl Create for HeightRangeMap {
    fn create() -> Self {
        IndexMap::from([
            ("Overworld".to_string(), HeightRange { min: -64, max: 320 }),
            ("Nether".to_string(),    HeightRange { min: 0,   max: 128 }),
            ("TheEnd".to_string(),    HeightRange { min: 0,   max: 256 }),
        ])
    }
}

static CONFIG_DIR: OnceLock<String> = OnceLock::new();
const CONFIG_FILE: &str = "dimensions.json";
const LOG_FILE: &str = "log.txt";

pub fn config_path() -> Option<PathBuf> { CONFIG_DIR.get().map(|d| Path::new(d).join(CONFIG_FILE)) }
pub fn log_path() -> Option<PathBuf> { CONFIG_DIR.get().map(|d| Path::new(d).join(LOG_FILE)) }

fn set_config_dir(path: String) {
    if CONFIG_DIR.set(path).is_err() { log::error!("CONFIG_DIR can only be set once"); }
}

pub fn save(map: &HeightRangeMap) -> Result<(), ()> {
    let path = config_path().ok_or_else(|| log::error!("CONFIG_DIR is not set"))?;
    let json = soml::to_string(map).map_err(|e| log::error!("Serialize failed: {e}"))?;
    fs::write(path, json).map_err(|e| log::error!("Write failed: {e}"))?;
    Ok(())
}

fn align_down(v: i16) -> i16 { v - v.rem_euclid(16) }
fn align_up(v: i16) -> i16 { let r = v.rem_euclid(16); if r == 0 { v } else { v + (16 - r) } }

pub fn get(name: &str, min: i16, max: i16) -> (i16, i16) {
    let Some(path) = config_path() else {
        log::error!("CONFIG_DIR not set");
        return (min, max);
    };

    let content = fs::read_to_string(&path).unwrap_or_default();
    let mut map: HeightRangeMap = if content.is_empty() {
        HeightRangeMap::create()
    } else {
        soml::from_str(&content).unwrap_or_else(|e| {
            log::error!("Failed to parse config: {e}, regenerating defaults");
            HeightRangeMap::create()
        })
    };

    let result = match map.get(name) {
        Some(e) => {
            let new_min = align_down(e.min);
            let new_max = align_up(e.max);
            if e.min != new_min { log::info!("{name} min {} not divisible by 16, aligning to {new_min}", e.min); }
            if e.max != new_max { log::info!("{name} max {} not divisible by 16, aligning to {new_max}", e.max); }
            (new_min, new_max)
        }
        None => (min, max),
    };

    let mut changed = false;

    match map.entry(name.to_string()) {
        Entry::Occupied(mut e) => {
            if e.get().min != result.0 || e.get().max != result.1 {
                e.insert(HeightRange { min: result.0, max: result.1 });
                changed = true;
            }
        }
        Entry::Vacant(e) => {
            e.insert(HeightRange { min: result.0, max: result.1 });
            changed = true;
        }
    }

    if changed {
        save(&map).ok();
    }

    result
}

pub fn is_dir_writable(dir: &str) -> bool {
    let path = Path::new(dir);
    if let Err(e) = fs::create_dir_all(path) {
        if e.kind() != std::io::ErrorKind::AlreadyExists { return false; }
    }
    let test = path.join(".write_test");
    fs::write(&test, "").map(|_| { let _ = fs::remove_file(&test); true }).unwrap_or(false)
}

pub fn init_config(path: &mut String) {
    path.push_str("/BuildLimitChanger/");
    if !is_dir_writable(path) { return log::error!("Config directory not writable: {path}"); }
    set_config_dir(path.clone());
    if !config_path().map_or(false, |p| p.exists()) {
        save(&HeightRangeMap::create()).ok();
    }
}