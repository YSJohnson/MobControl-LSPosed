# MobCtrl

Mob Control 的 LSPosed 模块，针对游戏 3.25.2 的 arm64-v8a 版本开发。模块在游戏内提供悬浮面板，支持射速倍率、跳过插屏广告、临时金币和跳过券，以及精英俱乐部开关等功能。

射速和临时金币已在设备上观察到效果；精英俱乐部等功能仍需手动验证。精英俱乐部开关影响客户端状态读取，服务器验证的权益是否可用尚未确认。

## 使用

1. 安装模块，在 LSPosed 中启用并勾选 Mob Control 作用域。
2. 重启游戏，等待游戏加载完成。
3. 展开游戏内悬浮面板，选择功能并点击“进入游戏后启动功能”。

## 构建

Windows 手动构建需要 JDK 17 或更高版本、Android SDK（platforms 和 build-tools）及 Android NDK。设置 `JAVA_HOME`、`ANDROID_SDK_ROOT` 后，在仓库根目录运行：

```powershell
.\pack.ps1
```

APK 输出到 `build_out/MobCtrl-v1.0.apk`。该脚本会清空 `build_out`；若根目录不存在 `mobctrl.keystore`，它会生成新的本地签名密钥。覆盖安装已有模块时必须使用原签名密钥，密钥请自行安全保存。

也可用 Android Studio 打开 `module/`，或者安装 Gradle 后运行 `module/build.ps1`。Gradle 构建的签名配置与手动打包可能不同。

## 项目结构

- `module/`：模块源码、资源与 Gradle 配置。
- `stubs/`：手动编译使用的 Xposed API 桩。
- `pack.ps1`：Windows 手动打包脚本。

运行日志可通过 `adb logcat -s MobCtrl` 查看，也可在游戏文件目录的 `MobCtrl/native.log` 中查看。
