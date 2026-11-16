package com.yg.mobctrl;

import android.app.Application;
import android.content.Context;
import android.os.Environment;

import java.io.FileOutputStream;
import java.io.File;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.IXposedHookZygoteInit;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/**
 * Mob Control 辅助模块入口。
 *
 * 注入时机：Application.attach(Context) 之后立刻加载 native。
 * libil2cpp.so 此时还没被 Unity 加载，所以 native 侧起一个线程轮询 /proc/self/maps，
 * 等 libil2cpp.so 出现后再做 dlsym 与功能安装。
 */
public class MainHook implements IXposedHookLoadPackage, IXposedHookZygoteInit {

    public static final String TAG = "MobCtrl";
    private static final String PKG = "com.vincentb.MobControl";
    private static String sModulePath = null;
    private static boolean sStarted = false;
    private static boolean sLoaded = false;

    @Override
    public void initZygote(StartupParam startupParam) {
        sModulePath = startupParam.modulePath;
        log("initZygote, module=" + sModulePath);
    }

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lp) {
        if (!PKG.equals(lp.packageName)) return;
        log("handleLoadPackage proc=" + lp.processName + " first=" + lp.isFirstApplication);

        boolean ok = hookAttach(lp);
        if (!ok) {
            log("hook Application.attach failed, fallback to activity onCreate");
        }
        hookActivity(lp);
        hookOverlay(lp);
    }

    /**
     * 悬浮窗挂载点。真正的显示逻辑在 OverlayPanel 里：
     * 直接把 View 加到 Activity 的 decorView，不需要 SYSTEM_ALERT_WINDOW。
     */
    private void hookOverlay(XC_LoadPackage.LoadPackageParam lp) {
        try {
            Class<?> act = XposedHelpers.findClass(
                    "com.vincentb.mobcontrol.VkQualityUnityPlayerActivity", lp.classLoader);
            Class<?> unityAct = XposedHelpers.findClass(
                    "com.unity3d.player.UnityPlayerActivity", lp.classLoader);
            log("hookOverlay target=" + act.getName());

            XposedHelpers.findAndHookMethod(unityAct, "onResume", new XC_MethodHook() {
                @Override
                protected void afterHookedMethod(MethodHookParam param) {
                    if (!act.isInstance(param.thisObject)) return;
                    try {
                        OverlayPanel.attach((android.app.Activity) param.thisObject);
                    } catch (Throwable t) {
                        log("onResume overlay: " + t);
                    }
                }
            });

            XposedHelpers.findAndHookMethod(unityAct, "onDestroy", new XC_MethodHook() {
                @Override
                protected void afterHookedMethod(MethodHookParam param) {
                    if (!act.isInstance(param.thisObject)) return;
                    try {
                        OverlayPanel.detachFrom((android.app.Activity) param.thisObject);
                    } catch (Throwable t) {
                        log("onDestroy overlay: " + t);
                    }
                }
            });
            log("hookOverlay installed");
        } catch (Throwable t) {
            log("hookOverlay error: " + t);
        }
    }

    // ------------------------------------------------------------------ //

    private boolean hookAttach(XC_LoadPackage.LoadPackageParam lp) {
        try {
            XposedHelpers.findAndHookMethod(Application.class, "attach", Context.class,
                    new XC_MethodHook() {
                        @Override
                        protected void afterHookedMethod(MethodHookParam param) {
                            Context ctx = (Context) param.args[0];
                            if (ctx != null && ctx.getApplicationContext() != null)
                                bootstrap(ctx, "Application.attach");
                        }
                    });
            return true;
        } catch (Throwable t) {
            log("hookAttach error: " + t);
            return false;
        }
    }

    private void hookActivity(XC_LoadPackage.LoadPackageParam lp) {
        try {
            Class<?> act = XposedHelpers.findClass(
                    "com.vincentb.mobcontrol.VkQualityUnityPlayerActivity", lp.classLoader);
            XposedHelpers.findAndHookMethod(act, "onCreate", android.os.Bundle.class,
                    new XC_MethodHook() {
                        @Override
                        protected void afterHookedMethod(MethodHookParam param) {
                            Context ctx = (Context) param.thisObject;
                            if (ctx != null) bootstrap(ctx, "onCreate");
                        }
                    });
        } catch (Throwable t) {
            log("hookActivity error: " + t);
        }
    }

    /** 真正拉起 native。只跑一次。 */
    private static synchronized void bootstrap(Context ctx, String from) {
        if (sLoaded) return;
        log("bootstrap from " + from + ", pkg=" + ctx.getPackageName());
        Config.init(ctx);

        if (!NativeBridge.load(ctx, sModulePath)) {
            log("native load failed, abort");
            return;
        }
        sLoaded = true;
        log("native library loaded; waiting for manual start");
    }

    public static synchronized boolean startFeatures(Context ctx) {
        if (sStarted) return true;
        bootstrap(ctx, "panel");
        if (!sLoaded) return false;
        String outDir = pickOutDir(ctx);
        try {
            NativeBridge.start(
                    Config.mask(),
                    Config.getInt("fire_rate_pct", 300),
                    Config.getInt("mob_multi_pct", 500),
                    Config.getInt("damage_pct", 1000),
                    (long) Config.getInt("currency_amount", 1000000),
                    Config.getInt("target_level", 100),
                    outDir);
            sStarted = true;
            log("native start() returned, outDir=" + outDir);
            return true;
        } catch (Throwable t) {
            log("native start error: " + t);
            return false;
        }
    }

    /** 优先用 App 外部目录，免存储权限。 */
    private static String pickOutDir(Context ctx) {
        File dir = null;
        try {
            File ext = ctx.getExternalFilesDir(null);
            if (ext != null) dir = new File(ext, "MobCtrl");
        } catch (Throwable ignored) {}
        if (dir == null) {
            try {
                File pub = new File(Environment.getExternalStorageDirectory(), "MobCtrl");
                if (!pub.exists()) pub.mkdirs();
                if (pub.canWrite()) dir = pub;
            } catch (Throwable ignored) {}
        }
        if (dir == null) dir = new File(ctx.getFilesDir(), "MobCtrl");
        if (!dir.exists()) dir.mkdirs();
        return dir.getAbsolutePath();
    }

    // ------------------------------------------------------------------ //

    public static void log(String msg) {
        XposedBridge.log("[" + TAG + "] " + msg);
    }
}
