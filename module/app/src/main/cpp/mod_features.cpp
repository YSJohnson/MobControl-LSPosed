#include "mod_features.h"
#include "il2cpp_api.h"
#include "inline_hook.h"
#include "log.h"

#include <cstring>
#include <cstdio>
#include <string>
#include <atomic>

// 功能位，必须与 Java 侧 Config.java 保持一致
static constexpr uint32_t F_DUMP           = 1u << 0;
static constexpr uint32_t F_CHEATS_UNLOCK  = 1u << 1;
static constexpr uint32_t F_FIRE_RATE      = 1u << 2;
static constexpr uint32_t F_MOB_MULTI      = 1u << 3;
static constexpr uint32_t F_GATE_BOOST     = 1u << 4;
static constexpr uint32_t F_DAMAGE_MULTI   = 1u << 5;
static constexpr uint32_t F_INVINCIBLE     = 1u << 6;
static constexpr uint32_t F_ULTIMATE       = 1u << 7;
static constexpr uint32_t F_FORCE_COMPLETE = 1u << 8;
static constexpr uint32_t F_CURRENCY       = 1u << 9;
static constexpr uint32_t F_LEVEL          = 1u << 10;
static constexpr uint32_t F_NO_ADS         = 1u << 11;
static constexpr uint32_t F_SKIPETS        = 1u << 12;
static constexpr uint32_t F_ELITE_VIP      = 1u << 13;

static constexpr int   FieldStatic = 0x0010;   // FieldAttributes.Static

namespace {

// ===========================================================================
// 1. 常量返回桩
// ===========================================================================
static std::atomic<bool> g_cheats_enabled{false};
static void* g_cheats_constant = nullptr;
static std::atomic<bool (*)(void*)> g_cheats_orig[6]{};
static FieldInfo* g_cheats_fields[6]{};
static uint8_t g_cheats_field_original[6]{};
template<int I> bool stub_cheats(void* self) {
    if (g_cheats_enabled.load()) return true;
    auto orig = g_cheats_orig[I].load();
    return orig ? orig(self) : false;
}
static void* const kCheatsStubs[] = {
    (void*)stub_cheats<0>, (void*)stub_cheats<1>, (void*)stub_cheats<2>,
    (void*)stub_cheats<3>, (void*)stub_cheats<4>, (void*)stub_cheats<5>,
};

// 每个 hook 点一个独立的 orig 槽 + 倍率，宏生成避免串味。
// 注意：被 hook 的 getter 多为 IL2CPP 实例方法，隐藏的 this 在 x0。
// 桩函数必须原样接收这个 this 并转发给 trampoline，否则 orig 调用会拿到垃圾指针。
#define DEF_FLOAT_STUB(NAME, DEFVAL)                                  \
    static float (*g_##NAME##_orig)(void*) = nullptr;                 \
    static std::atomic<float> g_##NAME##_scale{1.0f};                 \
    float stub_##NAME(void* self) {                                   \
        float v = g_##NAME##_orig ? g_##NAME##_orig(self) : (DEFVAL); \
        return v * g_##NAME##_scale;                                  \
    }

#define DEF_INT_STUB(NAME, DEFVAL)                                    \
    static int (*g_##NAME##_orig)(void*) = nullptr;                   \
    static std::atomic<float> g_##NAME##_scale{1.0f};                 \
    static int  g_##NAME##_add  = 0;                                  \
    int stub_##NAME(void* self) {                                     \
        int v = g_##NAME##_orig ? g_##NAME##_orig(self) : (DEFVAL);   \
        return v * g_##NAME##_scale + g_##NAME##_add;                 \
    }

static std::atomic<float> g_fire_scale{1.0f};
static void* g_fire_update_orig = nullptr;
static std::atomic<bool> g_no_ads_enabled{false};
static std::atomic<bool> g_currency_enabled{false};
static std::atomic<bool> g_skipets_enabled{false};
static bool g_no_ads_ready = false;
static bool g_currency_ready = false;
static bool g_skipets_ready = false;
static void* g_no_ads_orig = nullptr;
static void* g_coins_orig = nullptr;
static void* g_spend_orig = nullptr;
static void* g_skipets_orig = nullptr;
static void* g_consume_skipets_orig = nullptr;
static std::atomic<bool> g_elite_vip_enabled{false};
static void* g_vip_type_orig = nullptr;
static void* g_vip_data_type_orig = nullptr;
static void* g_vip_access_orig = nullptr;

int stub_vip_type(void* self, const MethodInfo* method) {
    if (g_elite_vip_enabled.load()) return 2;
    auto original = (int (*)(void*, const MethodInfo*))__atomic_load_n(&g_vip_type_orig, __ATOMIC_ACQUIRE);
    return original(self, method);
}

int stub_vip_data_type(void* self, const MethodInfo* method) {
    if (g_elite_vip_enabled.load()) return 2;
    auto original = (int (*)(void*, const MethodInfo*))__atomic_load_n(&g_vip_data_type_orig, __ATOMIC_ACQUIRE);
    return original(self, method);
}

bool stub_vip_access(void* self, const MethodInfo* method) {
    if (g_elite_vip_enabled.load()) return true;
    auto original = (bool (*)(void*, const MethodInfo*))__atomic_load_n(&g_vip_access_orig, __ATOMIC_ACQUIRE);
    return original(self, method);
}

bool stub_no_ads(void* self, const MethodInfo* method) {
    if (g_no_ads_enabled.load()) return true;
    auto original = (bool (*)(void*, const MethodInfo*))__atomic_load_n(&g_no_ads_orig, __ATOMIC_ACQUIRE);
    return original(self, method);
}

int64_t stub_coins(void* self, const MethodInfo* method) {
    if (g_currency_enabled.load()) return INT64_C(100000);
    auto original = (int64_t (*)(void*, const MethodInfo*))__atomic_load_n(&g_coins_orig, __ATOMIC_ACQUIRE);
    return original(self, method);
}

int64_t stub_skipets(void* self, const MethodInfo* method) {
    if (g_skipets_enabled.load()) return INT64_C(100000);
    auto original = (int64_t (*)(void*, const MethodInfo*))__atomic_load_n(&g_skipets_orig, __ATOMIC_ACQUIRE);
    return original(self, method);
}

void stub_spend(void* self, int64_t amount, int32_t location, void* reason, void* extra, const MethodInfo* method) {
    auto original = (void (*)(void*, int64_t, int32_t, void*, void*, const MethodInfo*))__atomic_load_n(&g_spend_orig, __ATOMIC_ACQUIRE);
    original(self, g_currency_enabled.load() ? 0 : amount, location, reason, extra, method);
}

void stub_consume_skipets(void* self, int64_t amount, int32_t location, void* reason, void* extra, const MethodInfo* method) {
    auto original = (void (*)(void*, int64_t, int32_t, void*, void*, const MethodInfo*))__atomic_load_n(&g_consume_skipets_orig, __ATOMIC_ACQUIRE);
    original(self, g_skipets_enabled.load() ? 0 : amount, location, reason, extra, method);
}

void stub_fire_update(void* self, const MethodInfo* method) {
    auto original = (void (*)(void*, const MethodInfo*))__atomic_load_n(&g_fire_update_orig, __ATOMIC_ACQUIRE);
    if (!original) return;
    auto* shooter = static_cast<uint8_t*>(self);
    void* crowd = shooter ? *(void**)(shooter + 56) : nullptr;
    if (shooter && *(int32_t*)(shooter + 32) == 1 && crowd && *(int32_t*)((uint8_t*)crowd + 36) == 0) {
        float* speed = (float*)(shooter + 64);
        float previous = *speed;
        *speed = previous / g_fire_scale.load();
        original(self, method);
        *speed = previous;
    } else {
        original(self, method);
    }
}

DEF_FLOAT_STUB(damage,  10.0f)
DEF_FLOAT_STUB(hp,      100.0f)
DEF_FLOAT_STUB(cooldown, 10.0f)
DEF_INT_STUB(mob,       1)
DEF_INT_STUB(gate,      1)

// ===========================================================================
// 2. 通用工具
// ===========================================================================
std::string RetType(MethodInfo* m) {
    const Il2CppType* t = ic::method_get_return_type(m);
    if (!t || !ic::type_get_name) return "?";
    char* n = ic::type_get_name(t);
    return n ? std::string(n) : std::string("?");
}

// 把方法挂到桩上；要求零参
bool HookTo(MethodInfo* m, void* stub, void** origOut = nullptr) {
    if (!m) return false;
    const char* name = ic::method_get_name(m);
    uint32_t argc = ic::method_get_param_count(m);
    if (argc != 0) {
        LOGI("  skip %s: %u params", name, argc);
        return false;
    }
    void* target = ic::method_ptr(m);
    if (!target) {
        LOGW("  skip %s: method_ptr null", name);
        return false;
    }
    static void* hookedTargets[16] = {};
    static size_t hookedCount = 0;
    for (size_t i = 0; i < hookedCount; ++i) {
        if (hookedTargets[i] == target) {
            LOGW("  skip %s: target already hooked", name);
            return false;
        }
    }
    if (hookedCount == sizeof(hookedTargets) / sizeof(hookedTargets[0])) return false;
    void* orig = nullptr;
    if (!hook::install(target, stub, &orig)) {
        LOGW("  hook failed: %s @ %p", name, target);
        return false;
    }
    if (origOut) *origOut = orig;
    hookedTargets[hookedCount++] = target;
    LOGI("  hooked %s (%s) @ %p", name, RetType(m).c_str(), target);
    return true;
}

// 在类里按候选名逐个尝试，第一个零参 float getter 命中即 hook
bool HookFloatByNames(Il2CppClass* k, const char* const* names, int n,
                      void* stub, float (**origSlot)(void*)) {
    if (!k) return false;
    for (int i = 0; i < n; ++i) {
        MethodInfo* m = ic::find_method(k, names[i], 0);
        if (!m) continue;
        if (RetType(m) != "System.Single") continue;
        void* orig = nullptr;
        if (HookTo(m, stub, &orig)) {
            if (origSlot) *origSlot = (float (*)(void*))orig;
            return true;
        }
    }
    return false;
}

bool HookIntByNames(Il2CppClass* k, const char* const* names, int n,
                    void* stub, int (**origSlot)(void*)) {
    if (!k) return false;
    for (int i = 0; i < n; ++i) {
        MethodInfo* m = ic::find_method(k, names[i], 0);
        if (!m) continue;
        if (RetType(m) != "System.Int32") continue;
        void* orig = nullptr;
        if (HookTo(m, stub, &orig)) {
            if (origSlot) *origSlot = (int (*)(void*))orig;
            return true;
        }
    }
    return false;
}

// 取类的静态单例字段（Instance / instance / _instance / s_instance / Current）
Il2CppObject* GetSingleton(Il2CppClass* k) {
    static const char* kNames[] = {
        "Instance", "instance", "_instance", "s_instance", "Current", "_current",
    };
    for (const char* n : kNames) {
        FieldInfo* f = ic::find_field(k, n);
        if (!f) continue;
        if (ic::field_get_flags && !(ic::field_get_flags(f) & FieldStatic)) continue;
        Il2CppObject* obj = nullptr;
        ic::field_static_get_value(f, &obj);
        if (obj) {
            LOGI("  singleton %s.%s = %p", ic::full_name(k).c_str(), n, obj);
            return obj;
        }
    }
    return nullptr;
}

// 打印一个类的全部成员（诊断用）
void DescribeClass(Il2CppClass* k) {
    if (!k) return;
    LOGI("---- %s ----", ic::full_name(k).c_str());

    void* it = nullptr;
    FieldInfo* f;
    while ((f = ic::class_get_fields(k, &it))) {
        uint32_t fl = ic::field_get_flags ? ic::field_get_flags(f) : 0;
        LOGI("  F %s %s (off=%zu%s)", ic::field_get_name(f),
             ic::type_get_name ? ic::type_get_name(ic::field_get_type(f)) : "?",
             ic::field_get_offset(f), (fl & FieldStatic) ? ", static" : "");
    }

    void* it2 = nullptr;
    const MethodInfo* m;
    while ((m = ic::class_get_methods(k, &it2))) {
        const char* mn = ic::method_get_name(m);
        if (!mn) continue;
        LOGI("  M %s/%u -> %s", mn, ic::method_get_param_count(m), RetType(const_cast<MethodInfo*>(m)).c_str());
    }
}

// ===========================================================================
// 3. 官方 Cheats 系统
// ===========================================================================
// 门控候选：任何一处被强制为 true，官方作弊面板就有可能放出来
struct GateTarget { const char* ns; const char* cls; const char* member; };

const GateTarget kGates[] = {
    {"_Game.Common.MamboCore",  "CheatConfig",        "CheatsAreEnabled"},
    {"_Game.Common.MamboCore",  "CheatConfig",        "UseCheatsConfig"},
    {"_Game.Common.MamboCore",  "CheatsNotExposed",   "CheatsAreEnabled"},
    {"_Game.Common.Cheats",     "CheatsManager",      "CheatsAreEnabled"},
    {"_Game.Common.Cheats",     "CheatsManager",      "IsCheatsEnabled"},
    {"_Game.Common.CheatsMenu", "TabManager",         "CheatsAreEnabled"},
};

void UnlockCheats() {
    LOGI("=== [1] unlock official cheats ===");
    int forced = 0;
    g_cheats_enabled = true;

    for (size_t i = 0; i < sizeof(kGates) / sizeof(kGates[0]); ++i) {
        const auto& g = kGates[i];
        Il2CppClass* k = ic::find_class(g.ns, g.cls);
        if (!k) continue;

        // (a) 属性 getter：hook 成恒 true
        MethodInfo* m = ic::find_method(k, g.member, 0);
        if (!m) {
            std::string getter = std::string("get_") + g.member;
            m = ic::find_method(k, getter.c_str(), 0);
        }
        if (m) {
            if (strcmp(g.cls, "CheatsManager") == 0 && strcmp(g.member, "CheatsAreEnabled") == 0
                    && RetType(m) == "System.Boolean") {
                void* target = ic::method_ptr(m);
                if (hook::set_constant_bool(target, true)) {
                    g_cheats_constant = target;
                    ++forced;
                }
                continue;
            }
            void* orig = nullptr;
            if (RetType(m) == "System.Boolean" && HookTo(m, kCheatsStubs[i], &orig)) {
                g_cheats_orig[i] = (bool (*)(void*))orig;
                ++forced;
            }
            continue;
        }

        // (b) 静态 bool 字段：直接写
        FieldInfo* f = ic::find_field(k, g.member);
        const Il2CppType* type = f && ic::field_get_type ? ic::field_get_type(f) : nullptr;
        const char* typeName = type && ic::type_get_name ? ic::type_get_name(type) : nullptr;
        if (f && ic::field_get_flags && (ic::field_get_flags(f) & FieldStatic)
                && typeName && strcmp(typeName, "System.Boolean") == 0
                && ic::field_static_get_value && ic::field_static_set_value) {
            ic::field_static_get_value(f, &g_cheats_field_original[i]);
            g_cheats_fields[i] = f;
            uint8_t one = 1;
            ic::field_static_set_value(f, &one);
            LOGI("  static %s.%s := true", ic::full_name(k).c_str(), g.member);
            ++forced;
            continue;
        }

        DescribeClass(k);
    }
    LOGI("unlock_cheats: %d gate(s) forced", forced);

    // 尝试主动初始化一次 cheat 系统
    Il2CppClass* mgr = ic::find_class("_Game.Common.Cheats", "CheatsManager");
    if (mgr) {
        const char* inits[] = {"Init", "Initialize", "Setup", "InitCheats", "CreateCheats", "AddCheats"};
        Il2CppObject* inst = GetSingleton(mgr);
        for (const char* n : inits) {
            MethodInfo* m = ic::find_method(mgr, n, 0);
            if (!m) continue;
            bool isInst = ic::method_is_instance && ic::method_is_instance(m);
            if (isInst && !inst) { LOGW("  %s is instance method, no singleton", n); continue; }
            Il2CppObject* ex = nullptr;
            ic::runtime_invoke(m, isInst ? inst : nullptr, nullptr, &ex);
            LOGI("  invoke %s -> %s", n, ex ? "EXCEPTION" : "ok");
            break;
        }
    }
}

// 调用指定的无参作弊方法
int InvokeCheatMethods(const char* ns, const char* cls, const char* method) {
    Il2CppClass* k = ic::find_class(ns, cls);
    if (!k) return 0;

    Il2CppObject* inst = GetSingleton(k);
    int n = 0;
    void* it = nullptr;
    const MethodInfo* m;
    while ((m = ic::class_get_methods(k, &it))) {
        const char* mn = ic::method_get_name(m);
        if (!mn || strcmp(mn, method) != 0) continue;
        if (ic::method_get_param_count(m) != 0) continue;

        bool isInst = ic::method_is_instance && ic::method_is_instance(m);
        if (isInst && !inst) continue;

        Il2CppObject* ex = nullptr;
        ic::runtime_invoke(const_cast<MethodInfo*>(m), isInst ? inst : nullptr, nullptr, &ex);
        LOGI("  %s.%s -> %s", cls, mn, ex ? "EXCEPTION" : "ok");
        ++n;
    }
    return n;
}

// ===========================================================================
// 4. 局内玩法
// ===========================================================================
void DoFireRate(int pct) {
    if (pct <= 0) pct = 100;
    // 间隔 = 原间隔 * (100 / pct)，即射速倍率 pct/100
    g_fire_scale = 100.0f / (float)pct;

    Il2CppClass* shooter = ic::find_class("_Game.Gameplay.Shoot", "Shooter");
    Il2CppClass* crowd = ic::find_class("", "CrowdController");
    FieldInfo* origin = shooter ? ic::find_field(shooter, "m_OriginType") : nullptr;
    FieldInfo* controller = shooter ? ic::find_field(shooter, "m_CrowdController") : nullptr;
    FieldInfo* speed = shooter ? ic::find_field(shooter, "m_DefaultChargeSpeed") : nullptr;
    FieldInfo* team = crowd ? ic::find_field(crowd, "m_TeamID") : nullptr;
    MethodInfo* update = shooter ? ic::find_method(shooter, "UpdateCharge", 0) : nullptr;
    // shortcut: supports the verified 3.25.2 layout; update after validating a new game build.
    if (!origin || !controller || !speed || !team || !update || RetType(update) != "System.Void"
            || ic::field_get_offset(origin) != 32 || ic::field_get_offset(controller) != 56
            || ic::field_get_offset(speed) != 64 || ic::field_get_offset(team) != 36
            || !hook::install_single(ic::method_ptr(update), (void*)stub_fire_update,
                                     &g_fire_update_orig, 0xf81e0ffeu)) {
        LOGW("fire_rate: unsupported shooter layout or hook failed");
        return;
    }
    LOGI("fire_rate: player shooter charge hook installed");
}

void DoMobMulti(int pct) {
    if (pct <= 0) pct = 100;
    g_mob_scale = pct / 100.0f;

    static const char* const kNames[] = {
        "get_Count", "get_Amount", "get_Quantity", "get_amount",
    };
    const char* classes[][2] = {
        {"_Game.Gameplay.Mobs", "MobsManager"},
        {"_Game.Gameplay.Mobs", "MobConfig"},
        {"_Game.Gameplay.Core", "UnitParameters"},
    };
    for (auto& c : classes) {
        Il2CppClass* k = ic::find_class(c[0], c[1]);
        if (!k) continue;
        if (HookIntByNames(k, kNames, (int)(sizeof(kNames) / sizeof(kNames[0])),
                           (void*)stub_mob, &g_mob_orig)) {
            LOGI("mob_multi: hooked on %s.%s, x%.2f", c[0], c[1], g_mob_scale.load());
            return;
        }
    }
    LOGW("mob_multi: no hook point found");
}

void DoDamageMulti(int pct) {
    if (pct <= 0) pct = 100;
    g_damage_scale = pct / 100.0f;

    static const char* const kNames[] = {
        "get_Damage", "get_DamageMultiplier", "get_damage", "get_BaseDamage",
    };
    const char* classes[][2] = {
        {"_Game.Gameplay.Cannons.Logic", "MobStrengthModifier"},
        {"_Game.Gameplay.GateCollection", "DamageResourcesCollector"},
        {"_Game.Gameplay", "AttackDamageConfig"},
    };
    for (auto& c : classes) {
        Il2CppClass* k = ic::find_class(c[0], c[1]);
        if (!k) continue;
        if (HookFloatByNames(k, kNames, (int)(sizeof(kNames) / sizeof(kNames[0])),
                             (void*)stub_damage, &g_damage_orig)) {
            LOGI("damage_multi: hooked on %s.%s, x%.2f", c[0], c[1], g_damage_scale.load());
            return;
        }
    }
    LOGW("damage_multi: no hook point found");
}

void DoInvincible() {
    // 我方小兵血量/受伤入口：把血量 getter 拉到极大值
    g_hp_scale = 1000.0f;
    static const char* const kNames[] = {"get_Health", "get_Life", "get_maxHealth", "get_HealthMax"};
    const char* classes[][2] = {
        {"_Game.Gameplay.Cannons.Logic", "MobStrengthModifier"},
        {"_Game.Gameplay.Core", "UnitParameters"},
    };
    for (auto& c : classes) {
        Il2CppClass* k = ic::find_class(c[0], c[1]);
        if (!k) continue;
        if (HookFloatByNames(k, kNames, (int)(sizeof(kNames) / sizeof(kNames[0])),
                             (void*)stub_hp, &g_hp_orig)) {
            LOGI("invincible: hooked hp getter on %s.%s", c[0], c[1]);
            return;
        }
    }
    LOGW("invincible: no hook point found");
}

void DoUltimate() {
    g_cooldown_scale = 0.0f;
    static const char* const kNames[] = {
        "get_ChargeTime", "get_Cooldown", "get_cooldown", "get_ReloadTime",
    };
    const char* classes[][2] = {
        {"_Game.Gameplay.Cannons.Logic", "Cannon"},
        {"_Game.Gameplay.Ultimates.Common", "UltimateController"},
        {"_Game.Gameplay.Ultimates.Common.Scripts", "UltimatesManager"},
    };
    for (auto& c : classes) {
        Il2CppClass* k = ic::find_class(c[0], c[1]);
        if (!k) continue;
        if (HookFloatByNames(k, kNames, (int)(sizeof(kNames) / sizeof(kNames[0])),
                             (void*)stub_cooldown, &g_cooldown_orig)) {
            LOGI("ultimate: cooldown zeroed on %s.%s", c[0], c[1]);
            return;
        }
    }
    // 兜底：官方 CHEAT_FillUltimate 挂在 CheatsManager 上，由 InvokeCheatMethods 统一扫；
    // 这里只确认类型是否存在，不做副作用调用。
    Il2CppClass* um = ic::find_class("_Game.Gameplay.Ultimates.Common.Scripts", "UltimatesManager");
    LOGW("ultimate: no getter hooked (UltimatesManager=%p), rely on CHEAT_FillUltimate", (void*)um);
}

void DoGateBoost() {
    g_gate_scale = 3;
    static const char* const kNames[] = {"get_Multiplier", "get_Value", "get_Amount"};
    Il2CppClass* k = ic::find_class("_Game.Gameplay.GateEnhancement", "GateEnhancer");
    if (k && HookIntByNames(k, kNames, 3, (void*)stub_gate, &g_gate_orig))
        LOGI("gate_boost: hooked on GateEnhancer x%.2f", g_gate_scale.load());
    else
        LOGW("gate_boost: no hook point found");
}

void DoForceComplete() {
    LOGI("=== force complete ===");
    int n = InvokeCheatMethods("_Game.Common.Cheats", "CheatsManager", "CHEAT_ForceComplete");
    LOGI("force_complete: %d cheat method(s) invoked", n);
}

void DoNoAds() {
    Il2CppClass* k = ic::find_class("", "NoAdsManager");
    MethodInfo* m = k ? ic::find_method(k, "get_HasNoAds", 0) : nullptr;
    g_no_ads_ready = m && RetType(m) == "System.Boolean"
        && hook::install_single(ic::method_ptr(m), (void*)stub_no_ads, &g_no_ads_orig, 0xa9bf4ffeu);
    LOGI("no_ads: interstitial check %s", g_no_ads_ready ? "hooked" : "unavailable");
}

void DoEliteVip() {
    Il2CppClass* club = ic::find_class("_Game.Features.VIPClub", "VipClub");
    Il2CppClass* data = ic::find_class("_Game.Features.VIPClub", "VipDataManager");
    MethodInfo* type = club ? ic::find_method(club, "get_VipType", 0) : nullptr;
    MethodInfo* dataType = data ? ic::find_method(data, "get_VipType", 0) : nullptr;
    MethodInfo* access = club ? ic::find_method(club, "get_HasAccess", 0) : nullptr;
    if (type && RetType(type) == "_Game.Features.VIPClub.EVipType")
        hook::install_single(ic::method_ptr(type), (void*)stub_vip_type, &g_vip_type_orig, 0xf81f0ffeu);
    if (dataType && RetType(dataType) == "_Game.Features.VIPClub.EVipType")
        hook::install_single(ic::method_ptr(dataType), (void*)stub_vip_data_type, &g_vip_data_type_orig, 0xf9400808u);
    if (access && RetType(access) == "System.Boolean")
        hook::install_single(ic::method_ptr(access), (void*)stub_vip_access, &g_vip_access_orig, 0xa9bf4ffeu);
    LOGI("elite_vip: club=%d data=%d access=%d", g_vip_type_orig != nullptr,
         g_vip_data_type_orig != nullptr, g_vip_access_orig != nullptr);
}

void DoCurrency() {
    Il2CppClass* k = ic::find_class("_Game.Common.Currency", "CurrencyManager");
    MethodInfo* get = k ? ic::find_method(k, "get_Coins", 0) : nullptr;
    MethodInfo* spend = k ? ic::find_method(k, "RemoveCoins", 4) : nullptr;
    if (!get || !spend || RetType(get) != "System.Int64" || RetType(spend) != "System.Void") {
        LOGW("currency: unsupported methods");
        return;
    }
    const char* types[] = {"System.Int64", "EGameLocation", "System.String", "System.Collections.Generic.Dictionary<System.String,System.Object>"};
    for (uint32_t i = 0; i < 4; ++i) {
        const Il2CppType* type = ic::method_get_param(spend, i);
        const char* name = type ? ic::type_get_name(type) : nullptr;
        if (!name || strcmp(name, types[i]) != 0) {
            LOGW("currency: unsupported spend parameters");
            return;
        }
    }
    if (!hook::install_single(ic::method_ptr(spend), (void*)stub_spend, &g_spend_orig, 0xf81c0ffeu)) {
        LOGW("currency: spend hook unavailable");
        return;
    }
    g_currency_ready = hook::install_single(ic::method_ptr(get), (void*)stub_coins, &g_coins_orig, 0xf81f0ffeu);
    LOGI("currency: temporary balance %s", g_currency_ready ? "hooked" : "unavailable");
}

void DoSkipets() {
    Il2CppClass* k = ic::find_class("Skipets", "SkipetsManager");
    MethodInfo* get = k ? ic::find_method(k, "get_NbSkipetsOwned", 0) : nullptr;
    MethodInfo* consume = k ? ic::find_method(k, "Consume", 4) : nullptr;
    if (!get || !consume || RetType(get) != "System.Int64" || RetType(consume) != "System.Void") {
        LOGW("skipets: unsupported methods");
        return;
    }
    const char* types[] = {"System.Int64", "EGameLocation", "System.String", "System.Collections.Generic.Dictionary<System.String,System.Object>"};
    for (uint32_t i = 0; i < 4; ++i) {
        const Il2CppType* type = ic::method_get_param(consume, i);
        const char* name = type ? ic::type_get_name(type) : nullptr;
        if (!name || strcmp(name, types[i]) != 0) {
            LOGW("skipets: unsupported consume parameters");
            return;
        }
    }
    if (!hook::install_single(ic::method_ptr(consume), (void*)stub_consume_skipets, &g_consume_skipets_orig, 0xf81c0ffeu)) {
        LOGW("skipets: consume hook unavailable");
        return;
    }
    g_skipets_ready = hook::install_single(ic::method_ptr(get), (void*)stub_skipets, &g_skipets_orig, 0xf81f0ffeu);
    LOGI("skipets: temporary balance %s", g_skipets_ready ? "hooked" : "unavailable");
}

} // namespace

// ===========================================================================
// 5. 入口
// ===========================================================================
namespace feat {

void install_all(const FeatParams& p) {
    LOGI("install_all mask=0x%x fire=%d%% mob=%d%% dmg=%d%%",
         p.mask, p.fireRatePct, p.mobMulPct, p.dmgPct);

    if (p.mask & F_CHEATS_UNLOCK) UnlockCheats();

    if (p.mask & F_FIRE_RATE)      DoFireRate(p.fireRatePct);
    if (p.mask & F_MOB_MULTI)      DoMobMulti(p.mobMulPct);
    if (p.mask & F_DAMAGE_MULTI)   DoDamageMulti(p.dmgPct);
    if (p.mask & F_INVINCIBLE)     DoInvincible();
    if (p.mask & F_ULTIMATE)       DoUltimate();
    if (p.mask & F_GATE_BOOST)     DoGateBoost();
    if (p.mask & F_FORCE_COMPLETE) DoForceComplete();
    if (p.mask & F_NO_ADS)         DoNoAds();

    if (p.mask & F_CURRENCY) DoCurrency();
    if (p.mask & F_SKIPETS) DoSkipets();
    if (p.mask & F_ELITE_VIP) DoEliteVip();
    if (p.mask & F_LEVEL) {
        LOGI("level: target=%d (use official CHEATS_LevelUpTo with arg)", p.level);
    }

    LOGI("install_all done");
}

/**
 * 热更新：悬浮面板每改一次开关/滑块就调一次。
 * 已关掉的位必须回落到 1.0，否则用户关不掉。
 */
void update(const FeatParams& p) {
    g_no_ads_enabled = g_no_ads_ready && (p.mask & fbit::NO_ADS);
    g_currency_enabled = g_currency_ready && (p.mask & fbit::CURRENCY);
    g_skipets_enabled = g_skipets_ready && (p.mask & fbit::SKIPETS);
    g_elite_vip_enabled = (p.mask & fbit::ELITE_VIP) != 0;
    bool cheatsEnabled = (p.mask & fbit::CHEATS_UNLOCK) != 0;
    g_cheats_enabled = cheatsEnabled;
    if (g_cheats_constant) hook::set_constant_bool(g_cheats_constant, cheatsEnabled);
    for (size_t i = 0; i < sizeof(g_cheats_fields) / sizeof(g_cheats_fields[0]); ++i) {
        if (!g_cheats_fields[i]) continue;
        uint8_t value = cheatsEnabled ? 1 : g_cheats_field_original[i];
        ic::field_static_set_value(g_cheats_fields[i], &value);
    }
    g_fire_scale = (p.mask & fbit::FIRE_RATE)
                   ? (100.0f / (float)(p.fireRatePct > 0 ? p.fireRatePct : 100))
                   : 1.0f;
    g_mob_scale = (p.mask & fbit::MOB_MULTI)
                  ? ((p.mobMulPct > 0 ? p.mobMulPct : 100) / 100.0f)
                  : 1.0f;
    g_damage_scale = (p.mask & fbit::DAMAGE_MULTI)
                     ? ((p.dmgPct > 0 ? p.dmgPct : 100) / 100.0f)
                     : 1.0f;
    g_hp_scale     = (p.mask & fbit::INVINCIBLE) ? 1000.0f : 1.0f;
    g_cooldown_scale = (p.mask & fbit::ULTIMATE) ? 0.0f : 1.0f;
    g_gate_scale = (p.mask & fbit::GATE_BOOST)
                   ? ((p.gateBoostPct > 0 ? p.gateBoostPct : 100) / 100.0f)
                   : 1.0f;

    LOGI("update: mask=0x%x fire=%.2f mob=%.2f dmg=%.2f hp=%.1f cd=%.2f gate=%.2f",
         p.mask, g_fire_scale.load(), g_mob_scale.load(), g_damage_scale.load(),
         g_hp_scale.load(), g_cooldown_scale.load(), g_gate_scale.load());
}

} // namespace feat
