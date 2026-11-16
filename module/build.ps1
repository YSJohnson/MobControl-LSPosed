# MobCtrl 构建脚本
# 用法: .\build.ps1 [-Task assembleDebug|assembleRelease]

param(
    [string]$Task = "assembleRelease"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

function Fail($msg) {
    Write-Host "[x] $msg" -ForegroundColor Red
    exit 1
}

Write-Host "=== MobCtrl build ===" -ForegroundColor Cyan

# --- JDK ---
$javaHome = $env:JAVA_HOME
if (-not $javaHome -or -not (Test-Path (Join-Path $javaHome "bin\java.exe"))) {
    $cand = Get-ChildItem "C:\Users\$env:USERNAME\.jdks" -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "jdk-17*" -or $_.Name -like "jdk-21*" } |
            Sort-Object Name -Descending | Select-Object -First 1
    if ($cand) { $javaHome = $cand.FullName; $env:JAVA_HOME = $javaHome }
}
if (-not $javaHome) { Fail "找不到 JDK 17+，请设置 JAVA_HOME" }
Write-Host "[ok] JAVA_HOME = $javaHome"

# --- SDK ---
$sdk = $env:ANDROID_SDK_ROOT
if (-not $sdk) { $sdk = $env:ANDROID_HOME }
if (-not $sdk) {
    foreach ($p in @("C:\Android\Sdk", "$env:LOCALAPPDATA\Android\Sdk")) {
        if (Test-Path $p) { $sdk = $p; break }
    }
}
if (-not $sdk) { Fail "找不到 Android SDK，请设置 ANDROID_SDK_ROOT" }
$env:ANDROID_SDK_ROOT = $sdk
$env:ANDROID_HOME = $sdk
Write-Host "[ok] ANDROID_SDK_ROOT = $sdk"

# --- NDK ---
$ndkDir = Join-Path $sdk "ndk"
if (Test-Path $ndkDir) {
    $ndk = Get-ChildItem $ndkDir -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if ($ndk) { Write-Host "[ok] NDK = $($ndk.FullName)" }
    else { Write-Host "[!] $ndkDir 为空，AGP 会尝试自动下载 NDK" -ForegroundColor Yellow }
} else {
    Write-Host "[!] 没有 $ndkDir，AGP 会尝试自动下载 NDK 26.1.10909125" -ForegroundColor Yellow
}

# --- gradle ---
$gradle = Get-Command gradle -ErrorAction SilentlyContinue
$wrapper = Join-Path $root "gradlew.bat"

if (Test-Path $wrapper) {
    Write-Host "[ok] 使用 gradle wrapper"
    & $wrapper $Task
} elseif ($gradle) {
    Write-Host "[ok] 使用系统 gradle: $($gradle.Source)"
    & $gradle.Source $Task
} else {
    Write-Host ""
    Write-Host "[!] 既没有 gradlew 也没有 gradle 命令。三选一：" -ForegroundColor Yellow
    Write-Host "    1) 用 Android Studio 打开 $root，Build -> Build APK"
    Write-Host "    2) 安装 Gradle 8.7+ 后重跑此脚本"
    Write-Host "    3) 手动生成 wrapper:"
    Write-Host "       gradle wrapper --gradle-version 8.7"
    exit 1
}

$apk = Get-ChildItem (Join-Path $root "app\build\outputs\apk") -Recurse -Filter "*.apk" -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($apk) {
    Write-Host ""
    Write-Host "[ok] APK: $($apk.FullName)  ($([math]::Round($apk.Length/1KB,1)) KB)" -ForegroundColor Green
} else {
    Write-Host "[!] 没找到产物 APK" -ForegroundColor Yellow
}
