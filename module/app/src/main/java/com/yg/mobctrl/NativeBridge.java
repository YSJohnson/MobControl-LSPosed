package com.yg.mobctrl;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * native 桥。所有 native 符号都在 libmobctrl.so 里。
 *
 * 注意：模块代码跑在**目标应用**进程里，System.loadLibrary("mobctrl") 会去目标应用的
 * nativeLibraryDir 找库 —— 那里没有我们的 .so。所以必须从模块 APK 里把 arm64 的
 * .so 解出来落到应用私有目录，再 System.load(绝对路径)。
 */
public final class NativeBridge {

    private static final String TAG = "MobCtrl";
    private static final String LIB = "libmobctrl.so";
    private static boolean loaded = false;

    private NativeBridge() {}

    public static synchronized boolean load(Context ctx, String moduleApk) {
        if (loaded) return true;
        if (ctx == null || moduleApk == null) {
            Log.e(TAG, "load: ctx or moduleApk is null");
            return false;
        }
        try {
            File dir = ctx.getDir("mobctrl_lib", Context.MODE_PRIVATE);
            if (!dir.exists()) dir.mkdirs();
            File so = new File(dir, LIB);

            File tmp = new File(dir, LIB + "." + android.os.Process.myPid() + ".tmp");
            if (!extract(moduleApk, tmp)) return false;
            if (!tmp.renameTo(so)) {
                Log.e(TAG, "replace native library failed: " + so);
                tmp.delete();
                return false;
            }

            System.load(so.getAbsolutePath());
            loaded = true;
            Log.i(TAG, "loaded " + so.getAbsolutePath() + " (" + so.length() + " bytes)");
        } catch (Throwable t) {
            Log.e(TAG, "load native failed", t);
        }
        return loaded;
    }

    /** 从模块 APK 的 lib/<abi>/libmobctrl.so 取出并写到私有目录。 */
    private static boolean extract(String apkPath, File dst) {
        String abi = pickAbi();
        String entryName = "lib/" + abi + "/" + LIB;
        try (ZipFile zip = new ZipFile(apkPath)) {
            ZipEntry e = zip.getEntry(entryName);
            if (e == null) {
                // 兜底：扫一遍所有 abi 目录
                java.util.Enumeration<? extends ZipEntry> en = zip.entries();
                while (en.hasMoreElements()) {
                    ZipEntry z = en.nextElement();
                    if (z.getName().endsWith("/" + LIB)) { e = z; break; }
                }
            }
            if (e == null) {
                Log.e(TAG, "no " + LIB + " inside " + apkPath);
                return false;
            }
            try (InputStream in = zip.getInputStream(e);
                 OutputStream out = new FileOutputStream(dst)) {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
            Log.i(TAG, "extracted " + e.getName() + " -> " + dst.getAbsolutePath());
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "extract failed", t);
            return false;
        }
    }

    private static String pickAbi() {
        for (String a : Build.SUPPORTED_ABIS) {
            if (a != null && a.startsWith("arm64")) return a;
        }
        return "arm64-v8a";
    }

    /**
     * 启动 native 侧：等待 libil2cpp.so 出现，解析 API，安装功能。
     *
     * @param mask       功能位图（见 Config）
     * @param fireRate   射速百分比（100 = 原速，300 = 三倍）
     * @param mobMulti   小兵数量百分比（100 = 原始）
     * @param damage     伤害百分比（100 = 原始）
     * @param currency   注入货币数量
     * @param level      目标等级
     * @param outDir     dump / 日志目录
     */
    public static native void start(int mask, int fireRate, int mobMulti, int damage,
                                    long currency, int level, String outDir);

    public static native void stop();

    /** 运行时改开关（悬浮面板 / 设置页用）。 */
    public static native void setMask(int mask);

    /**
     * 运行时热更新全部参数：改 mask + 各倍率，不重启游戏即生效。
     * native 侧只改全局标量，不做重复 hook。
     */
    public static native void apply(int mask, int fireRate, int mobMulti, int gateBoost,
                                    int damage, long currency, int level);

    /** 手动触发一次 dump。 */
    public static native boolean dumpNow();

    public static native void nLog(String msg);
}
