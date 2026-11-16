package com.yg.mobctrl;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * 模块配置。UI 与 native 之间的唯一样式来源。
 * 所有开关合并成一个位图传给 native，避免频繁 JNI 调用。
 */
public final class Config {

    // ---- 功能位 ----
    public static final int F_DUMP           = 1 << 0;  // 运行时自省 dump
    public static final int F_CHEATS_UNLOCK  = 1 << 1;  // 强制开启官方 CheatsMenu
    public static final int F_FIRE_RATE      = 1 << 2;  // 射速倍率
    public static final int F_MOB_MULTI      = 1 << 3;  // 小兵数量倍率
    public static final int F_GATE_BOOST     = 1 << 4;  // 门效果强化
    public static final int F_DAMAGE_MULTI   = 1 << 5;  // 伤害倍率
    public static final int F_INVINCIBLE     = 1 << 6;  // 我方小兵不减
    public static final int F_ULTIMATE       = 1 << 7;  // 大招无 CD
    public static final int F_FORCE_COMPLETE = 1 << 8;  // 强制通关
    public static final int F_CURRENCY       = 1 << 9;  // 货币注入
    public static final int F_LEVEL          = 1 << 10; // 等级设置
    public static final int F_NO_ADS         = 1 << 11; // 去广告
    public static final int F_SKIPETS        = 1 << 12; // 无限跳过卷
    public static final int F_ELITE_VIP      = 1 << 13; // 精英俱乐部

    /** 位 → SharedPreferences key（顺序必须与 F_* 一致） */
    private static final String[] BIT_KEY = {
            "dump", "cheats_unlock", "fire_rate", "mob_multi",
            "gate_boost", "damage_multi", "invincible", "ultimate",
            "force_complete", "currency", "level", "no_ads", "skipets", "elite_vip",
    };
    /** 位 → 默认值 */
    private static final boolean[] BIT_DEF = {
            false, true, false, false,
            false, false, false, false, false,
            false, false, false, false, false,
    };

    private static final String PREF = "mobctrl";
    private static SharedPreferences sSp;

    private Config() {}

    public static synchronized void init(Context ctx) {
        if (sSp == null && ctx != null) {
            sSp = ctx.getApplicationContext().getSharedPreferences(PREF, Context.MODE_PRIVATE);
            sSp.edit().remove("mob_multi").remove("gate_boost").remove("damage_multi")
                    .remove("invincible").remove("ultimate").remove("force_complete").apply();
        }
    }

    public static SharedPreferences sp() {
        return sSp;
    }

    // ---- 基本存取 ----

    public static boolean getBool(String key, boolean def) {
        return sSp != null && sSp.getBoolean(key, def);
    }

    public static void setBool(String key, boolean v) {
        if (sSp != null) sSp.edit().putBoolean(key, v).apply();
    }

    public static int getInt(String key, int def) {
        return sSp == null ? def : sSp.getInt(key, def);
    }

    public static void setInt(String key, int v) {
        if (sSp != null) sSp.edit().putInt(key, v).apply();
    }

    // ---- 位图 ----

    /** 某个功能位对应的 SP key。 */
    public static String keyForBit(int bit) {
        for (int i = 0; i < BIT_KEY.length; i++) {
            if ((1 << i) == bit) return BIT_KEY[i];
        }
        return "unknown_" + bit;
    }

    /** 某个功能位的默认开关。 */
    public static boolean defForBit(int bit) {
        for (int i = 0; i < BIT_KEY.length; i++) {
            if ((1 << i) == bit) return BIT_DEF[i];
        }
        return false;
    }

    /** 打包成 native 侧识别的位图。 */
    public static int mask() {
        int m = 0;
        for (int i = 0; i < BIT_KEY.length; i++) {
            if (getBool(BIT_KEY[i], BIT_DEF[i])) m |= (1 << i);
        }
        return m;
    }
}
