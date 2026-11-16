#include "log.h"
#include <pthread.h>
#include <cstdarg>
#include <cstring>

namespace mlog {

static FILE*           g_fp = nullptr;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

void set_file(const char* path) {
    if (!path) return;
    pthread_mutex_lock(&g_mu);
    if (g_fp) { fclose(g_fp); g_fp = nullptr; }
    g_fp = fopen(path, "a");
    if (g_fp) {
        setvbuf(g_fp, nullptr, _IOLBF, 0);
        fprintf(g_fp, "\n==== session start ====\n");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, MCTAG, "cannot open native log: %s", path);
    }
    pthread_mutex_unlock(&g_mu);
}

void line(const char* fmt, ...) {
    pthread_mutex_lock(&g_mu);
    if (g_fp) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(g_fp, fmt, ap);
        va_end(ap);
        fputc('\n', g_fp);
    }
    pthread_mutex_unlock(&g_mu);
}

} // namespace mlog
