#pragma once

#include <string>

std::string getExecutableDir();

#if defined(__ANDROID__)
#include <jni.h>
bool getConfigLocation(char *outPath, size_t outSize);
#endif

#if defined(_WIN32)
std::string GetStateDirectory();
#endif
