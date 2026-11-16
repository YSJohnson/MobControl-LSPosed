#pragma once
#include <cstdint>

// native 侧功能开关快照。Java 层 Config.mask() 打包位图，参数单独传。
struct FeatParams {
    uint32_t    mask;
    int         fireRatePct;   // 100 = 原速，200 = 两倍射速（间隔减半）
    int         mobMulPct;     // 100 = 原始数量
    int         gateBoostPct;  // 100 = 原始门数值，300 = 三倍
    int         dmgPct;        // 100 = 原始伤害
    long        currency;      // 目标货币值
    int         level;         // 目标等级
    const char* outDir;        // dump 输出目录
};

// 与 Java Config.F_* 必须逐位一致
namespace fbit {
constexpr uint32_t DUMP           = 1u << 0;
constexpr uint32_t CHEATS_UNLOCK  = 1u << 1;
constexpr uint32_t FIRE_RATE      = 1u << 2;
constexpr uint32_t MOB_MULTI      = 1u << 3;
constexpr uint32_t GATE_BOOST     = 1u << 4;
constexpr uint32_t DAMAGE_MULTI   = 1u << 5;
constexpr uint32_t INVINCIBLE     = 1u << 6;
constexpr uint32_t ULTIMATE       = 1u << 7;
constexpr uint32_t FORCE_COMPLETE = 1u << 8;
constexpr uint32_t CURRENCY       = 1u << 9;
constexpr uint32_t LEVEL          = 1u << 10;
constexpr uint32_t NO_ADS         = 1u << 11;
constexpr uint32_t SKIPETS        = 1u << 12;
constexpr uint32_t ELITE_VIP      = 1u << 13;
} // namespace fbit

namespace feat {
/** 首次安装：等 il2cpp 就绪后挂 hook。 */
void install_all(const FeatParams& p);

/** 热更新已安装功能的开关和倍率。 */
void update(const FeatParams& p);
}
