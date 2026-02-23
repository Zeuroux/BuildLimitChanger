pub fn combine_hex(max: i16, min: i16) -> i32 { 
    ((max as i32) << 16) | (min as u16 as i32)
}

pub fn split_hex(combined: i32) -> (i16, i16) {
    ((combined >> 16) as i16, (combined & 0xFFFF) as i16)
}

#[cfg_attr(target_os = "android", unsafe(no_mangle))]
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

#[cfg(target_os = "android")]
use jni::{objects::{GlobalRef, JObject, JString}, JNIEnv};

#[cfg(target_os = "android")]
pub fn get_games_directory(env: &mut JNIEnv) -> Option<String> {
    let env_class = env.find_class("android/os/Environment").ok()?;
    let storage_dir = env
        .call_static_method(env_class, "getExternalStorageDirectory", "()Ljava/io/File;", &[])
        .ok()?.l().ok()?;
    
    let mut result = get_absolute_path_from_file(env, storage_dir)?;
    result.push_str("/games");
    Some(result)
}

#[cfg(target_os = "android")]
pub fn get_app_external_files_dir(env: &mut JNIEnv, context: &JObject) -> Option<String> {
    let file_obj = env
        .call_method(context, "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;", &[(&JObject::null()).into()])
        .ok()?.l().ok()?;
    get_absolute_path_from_file(env, file_obj)
}

#[cfg(target_os = "android")]
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

#[cfg(target_os = "android")]
fn get_absolute_path_from_file(env: &mut JNIEnv, file_obj: JObject) -> Option<String> {
    let abs_path = env
        .call_method(file_obj, "getAbsolutePath", "()Ljava/lang/String;", &[])
        .ok()?.l().ok()?;
    env.get_string(&JString::from(abs_path)).ok().map(|s| s.into())
}