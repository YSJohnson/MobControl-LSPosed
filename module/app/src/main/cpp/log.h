#pragma once
#include <android/log.h>
#include <cstdarg>
#include <cstdio>

#define MCTAG "MobCtrl"

namespace mlog {
void set_file(const char* path);
void line(const char* fmt, ...);
}

// logcat + 文件双写：用户手机上没 adb 也能拿到完整日志
#define LOGI(...) do {                                          \
    __android_log_print(ANDROID_LOG_INFO,  MCTAG, __VA_ARGS__); \
    mlog::line(__VA_ARGS__);                                    \
} while (0)

#define LOGW(...) do {                                          \
    __android_log_print(ANDROID_LOG_WARN,  MCTAG, __VA_ARGS__); \
    mlog::line(__VA_ARGS__);                                    \
} while (0)

#define LOGE(...) do {                                          \
    __android_log_print(ANDROID_LOG_ERROR, MCTAG, __VA_ARGS__); \
    mlog::line(__VA_ARGS__);                                    \
} while (0)
