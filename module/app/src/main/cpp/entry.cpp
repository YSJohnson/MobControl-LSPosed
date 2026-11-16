#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <atomic>

#include "il2cpp_api.h"
#include "mod_features.h"
#include "dump.h"
#include "log.h"

// ---------------------------------------------------------------------------
// 全局参数由互斥锁保护，worker 负责安装和更新功能。
// ---------------------------------------------------------------------------
static char     g_outDir[512] = {0};
static int      g_mask        = 0;
static int      g_fireRate    = 100;
static int      g_mobMulti    = 100;
static int      g_gateBoost   = 200;
static int      g_damage      = 100;
static long     g_currency    = 0;
static int      g_level       = 1;
static bool g_started = false;
static std::atomic<bool> g_stop{false};
static std::atomic<bool> g_runtimeReady{false};
static pthread_mutex_t g_paramsMu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_paramsChanged = PTHREAD_COND_INITIALIZER;
static unsigned g_revision = 0;

static std::string OutDir() {
    return g_outDir[0] ? std::string(g_outDir)
                       : std::string("/sdcard/Android/data/com.vincentb.MobControl/files/MobCtrl");
}

/** 把当前全局参数打包成 FeatParams。 */
static FeatParams Snapshot() {
    FeatParams p{};
    p.mask         = (uint32_t)g_mask;
    p.fireRatePct  = g_fireRate;
    p.mobMulPct    = g_mobMulti;
    p.gateBoostPct = g_gateBoost;
    p.dmgPct       = g_damage;
    p.currency     = g_currency;
    p.level        = g_level;
    p.outDir       = g_outDir;
    return p;
}

// ---------------------------------------------------------------------------
// 等 Unity 把程序集全部加载进来：assemblies 数量稳定后即认为就绪
// ---------------------------------------------------------------------------
static bool WaitAssemblies(int timeoutSec) {
    for (int i = 0; i < timeoutSec; ++i) {
        if (g_stop) return false;
        Il2CppDomain* d = ic::domain_get ? ic::domain_get() : nullptr;
        if (d) {
            size_t n = 0;
            const Il2CppAssembly** as = ic::domain_get_assemblies(d, &n);
            if (as && n > 8) {
                LOGI("assemblies ready: %zu", n);
                return true;
            }
        }
        sleep(1);
    }
    LOGW("WaitAssemblies timeout");
    return false;
}

static void* Worker(void*) {
    LOGI("worker: waiting for libil2cpp.so ...");
    if (!ic::wait_and_init(120000)) {
        LOGE("worker: il2cpp never became available");
        return nullptr;
    }
    if (g_stop) return nullptr;

    if (!WaitAssemblies(60)) return nullptr;

    // IL2CPP API 必须在已挂到 GC 的线程上调用
    if (!ic::thread_attach(ic::domain_get())) {
        LOGE("worker: failed to attach to il2cpp domain");
        return nullptr;
    }
    g_runtimeReady = true;
    LOGI("worker: attached to il2cpp domain");

    uint32_t attempted = 0;
    unsigned seen = 0;
    while (!g_stop) {
        pthread_mutex_lock(&g_paramsMu);
        while (!g_stop && seen == g_revision)
            pthread_cond_wait(&g_paramsChanged, &g_paramsMu);
        if (g_stop) {
            pthread_mutex_unlock(&g_paramsMu);
            break;
        }
        FeatParams p = Snapshot();
        seen = g_revision;
        pthread_mutex_unlock(&g_paramsMu);

        uint32_t newlyEnabled = p.mask & ~attempted;
        if (newlyEnabled) {
            FeatParams install = p;
            install.mask = newlyEnabled;
            feat::install_all(install);
            if (newlyEnabled & fbit::DUMP) dump::write_all(g_outDir);
            attempted |= newlyEnabled;
        }
        feat::update(p);
    }
    LOGI("worker: stopped");
    return nullptr;
}

static void StartThread() {
    if (g_started) {
        LOGI("worker already started");
        return;
    }
    g_started = true;
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&th, &attr, Worker, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) g_started = false;
    LOGI("worker thread created rc=%d", rc);
}

// ---------------------------------------------------------------------------
// JNI
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_yg_mobctrl_NativeBridge_start(JNIEnv* env, jclass,
                                       jint mask, jint fireRate, jint mobMulti,
                                       jint damage, jlong currency, jint level,
                                       jstring outDir) {
    pthread_mutex_lock(&g_paramsMu);
    g_mask     = mask;
    g_fireRate = fireRate;
    g_mobMulti = mobMulti;
    g_damage   = damage;
    g_currency = (long)currency;
    g_level    = level;
    ++g_revision;
    pthread_mutex_unlock(&g_paramsMu);

    if (outDir) {
        const char* s = env->GetStringUTFChars(outDir, nullptr);
        if (s) {
            strncpy(g_outDir, s, sizeof(g_outDir) - 1);
            env->ReleaseStringUTFChars(outDir, s);
        }
    }
    mlog::set_file((OutDir() + "/native.log").c_str());

    LOGI("start: mask=0x%x out=%s", mask, g_outDir);
    StartThread();
}

extern "C" JNIEXPORT void JNICALL
Java_com_yg_mobctrl_NativeBridge_stop(JNIEnv*, jclass) {
    g_stop = true;
    pthread_mutex_lock(&g_paramsMu);
    pthread_cond_signal(&g_paramsChanged);
    pthread_mutex_unlock(&g_paramsMu);
    LOGI("stop requested (hooks stay installed)");
}

extern "C" JNIEXPORT void JNICALL
Java_com_yg_mobctrl_NativeBridge_setMask(JNIEnv*, jclass, jint mask) {
    pthread_mutex_lock(&g_paramsMu);
    g_mask = mask;
    ++g_revision;
    pthread_cond_signal(&g_paramsChanged);
    pthread_mutex_unlock(&g_paramsMu);
    LOGI("setMask 0x%x", mask);
}

/**
 * 悬浮面板的热更新入口。
 * 更新参数并唤醒 worker 安装新功能、刷新倍率。
 */
extern "C" JNIEXPORT void JNICALL
Java_com_yg_mobctrl_NativeBridge_apply(JNIEnv*, jclass,
                                       jint mask, jint fireRate, jint mobMulti,
                                       jint gateBoost, jint damage,
                                       jlong currency, jint level) {
    pthread_mutex_lock(&g_paramsMu);
    g_mask      = mask;
    g_fireRate  = fireRate;
    g_mobMulti  = mobMulti;
    g_gateBoost = gateBoost;
    g_damage    = damage;
    g_currency  = (long)currency;
    g_level     = level;
    ++g_revision;
    pthread_cond_signal(&g_paramsChanged);
    pthread_mutex_unlock(&g_paramsMu);
    LOGI("apply: hot-update mask=0x%x fire=%d%% mob=%d%% gate=%d%% dmg=%d%%",
         mask, fireRate, mobMulti, gateBoost, damage);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yg_mobctrl_NativeBridge_dumpNow(JNIEnv*, jclass) {
    if (!g_runtimeReady || !ic::thread_detach || !ic::image_get_name
            || !ic::image_get_class_count || !ic::image_get_class || !ic::class_get_name
            || !ic::class_get_namespace || !ic::class_get_parent || !ic::field_get_offset
            || !ic::field_get_type) {
        LOGW("dumpNow: il2cpp not ready");
        return JNI_FALSE;
    }
    void* thread = ic::thread_attach(ic::domain_get());
    if (!thread) {
        LOGE("dumpNow: thread attach failed");
        return JNI_FALSE;
    }
    bool success = dump::write_all(OutDir().c_str());
    ic::thread_detach(thread);
    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_yg_mobctrl_NativeBridge_nLog(JNIEnv* env, jclass, jstring msg) {
    if (!msg) return;
    const char* s = env->GetStringUTFChars(msg, nullptr);
    if (s) {
        LOGI("[java] %s", s);
        env->ReleaseStringUTFChars(msg, s);
    }
}

// ---------------------------------------------------------------------------
// 库加载时立刻把日志文件就位（此时还不知道 outDir，用默认路径试一次）
// ---------------------------------------------------------------------------
__attribute__((constructor)) static void OnLoad() {
    LOGI("libmobctrl.so loaded (pid=%d)", getpid());
}
