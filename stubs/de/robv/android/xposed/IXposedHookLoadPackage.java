package de.robv.android.xposed;

/**
 * 编译期 stub —— 仅供 javac 使用，运行时由 LSPosed 提供真实实现。
 * 本目录不参与打包，d8 只接受 com.yg.mobctrl 的 class 文件。
 */
public interface IXposedHookLoadPackage {
    void handleLoadPackage(de.robv.android.xposed.callbacks.XC_LoadPackage.LoadPackageParam lpparam) throws Throwable;
}
