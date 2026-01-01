use serde::{Deserialize, Serialize};
use std::{collections::HashMap, fs, path::{Path, PathBuf}, sync::OnceLock};

#[derive(Serialize, Deserialize, Debug)]
pub struct BuildLimit { pub min: i16, pub max: i16 }

type BuildLimitMap = HashMap<String, BuildLimit>;

static CONFIG_DIR: OnceLock<String> = OnceLock::new();
const CONFIG_FILE: &str = "dimensions.json";
const LOG_FILE: &str = "log.txt";

pub fn config_path() -> Option<PathBuf> { 
    CONFIG_DIR.get().map(|d| Path::new(d).join(CONFIG_FILE)) 
}
pub fn log_path() -> Option<PathBuf> { 
    CONFIG_DIR.get().map(|d| Path::new(d).join(LOG_FILE)) 
}

fn set_config_dir(path: String) { 
    if CONFIG_DIR.set(path).is_err() { log::error!("CONFIG_DIR can only be set once"); } 
}

pub fn save() -> Result<(), ()> {
    let defaults: BuildLimitMap = [
        ("Overworld", BuildLimit { min: -64, max: 320 }),
        ("Nether",    BuildLimit { min: 0,  max: 128 }),
        ("TheEnd",    BuildLimit { min: 0,  max: 256 })
    ].into_iter().map(|(k,v)| (k.to_string(), v)).collect();
    fs::write(config_path().ok_or_else(
        || { log::error!("CONFIG_DIR is not set"); () })?,
        serde_json::to_string_pretty(&defaults)
        .map_err(|e| log::error!("Serialize failed: {e}"))?)
        .map_err(|e| log::error!("Write failed: {e}"))?;
    Ok(())
}

pub fn load() -> BuildLimitMap {
    let path = match config_path() { 
        Some(p) => p,
        None => { 
            log::error!("CONFIG_DIR not set");
            return BuildLimitMap::new(); 
        } 
    };
    let content = fs::read_to_string(&path).unwrap_or_else(|_| {
        save().ok();
        fs::read_to_string(&path).unwrap_or_default() 
    });
    serde_json::from_str(&content).unwrap_or_else(|e| { 
        log::error!("Failed to parse config: {e}, regenerating default");
        save().ok(); BuildLimitMap::new() 
    })
}

pub fn init_config(path: &mut String) { 
    path.push_str("/BuildLimitChanger/");
    set_config_dir(path.clone());
    if !config_path().map_or(false, |p| p.exists()) { save().ok(); }
}