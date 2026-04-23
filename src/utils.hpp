#pragma once

#include <string>

std::string getExecutableDir();

#if defined(__ANDROID__)
#include <jni.h>
std::string ResolveGameStoragePath(JNIEnv* env);
#endif

#if defined(_WIN32)
std::string GetStateDirectory();
#endif
