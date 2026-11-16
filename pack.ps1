# MobCtrl 手动打包脚本（不依赖 Gradle / AGP）
# 依赖：JDK 17+、Android SDK build-tools、Android NDK
# 用法: .\pack.ps1

param(
    [string]$MinSdk   = "26",
    [string]$TargetSdk = "35",
    [string]$NdkHint  = "26.1.10909125"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$mod  = Join-Path $root "module\app\src\main"
$bld  = Join-Path $root "build_out"

function Step($m) { Write-Host "`n=== $m ===" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "[ok] $m" -ForegroundColor Green }
function Die($m)  { Write-Host "[x] $m" -ForegroundColor Red; exit 1 }

# ---------- 环境 ----------
Step "探测环境"

$javaHome = $env:JAVA_HOME
if (-not $javaHome -or -not (Test-Path (Join-Path $javaHome "bin\javac.exe"))) {
    $c = Get-ChildItem "C:\Users\$env:USERNAME\.jdks" -Directory -ErrorAction SilentlyContinue |
         Where-Object { $_.Name -like "jdk-17*" -or $_.Name -like "jdk-21*" } |
         Sort-Object Name -Descending | Select-Object -First 1
    if ($c) { $javaHome = $c.FullName; $env:JAVA_HOME = $javaHome }
}
if (-not $javaHome) { Die "找不到 JDK" }
$javac = Join-Path $javaHome "bin\javac.exe"
$keytool = Join-Path $javaHome "bin\keytool.exe"
Ok "JDK $javaHome"

$sdk = $env:ANDROID_SDK_ROOT
if (-not $sdk) { $sdk = "C:\Android\Sdk" }
if (-not (Test-Path $sdk)) { Die "找不到 Android SDK: $sdk" }

$bt = Get-ChildItem (Join-Path $sdk "build-tools") -Directory |
      Sort-Object { [int]($_.Name.Split('.')[0]) } -Descending | Select-Object -First 1
if (-not $bt) { Die "找不到 build-tools" }
$BT = $bt.FullName
Ok "build-tools $($bt.Name)"

$plat = Get-ChildItem (Join-Path $sdk "platforms") -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
if (-not $plat) { Die "找不到 platforms" }
$ANDROID_JAR = Join-Path $plat.FullName "android.jar"
Ok "android.jar $ANDROID_JAR"

$aapt2      = Join-Path $BT "aapt2.exe"
$d8         = Join-Path $BT "d8.bat"
$zipalign   = Join-Path $BT "zipalign.exe"
$apksigner  = Join-Path $BT "apksigner.bat"
foreach ($t in @($aapt2,$d8,$zipalign,$apksigner)) { if (-not (Test-Path $t)) { Die "缺工具 $t" } }

$ndkRoot = Join-Path $sdk "ndk"
if (-not (Test-Path $ndkRoot)) { Die "找不到 NDK 目录 $ndkRoot，先跑 sdkmanager --install `"ndk;$NdkHint`"" }
$ndk = Get-ChildItem $ndkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $ndk) { Die "NDK 目录为空" }
$clang = Join-Path $ndk.FullName "toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android$MinSdk-clang++.cmd"
if (-not (Test-Path $clang)) {
    $alt = Get-ChildItem (Join-Path $ndk.FullName "toolchains\llvm\prebuilt\windows-x86_64\bin") -Filter "aarch64-linux-android*-clang++.cmd" |
           Sort-Object Name | Select-Object -First 1
    if ($alt) { $clang = $alt.FullName } else { Die "找不到 NDK clang++" }
}
Ok "NDK $($ndk.Name)"

# ---------- 清理 ----------
if (Test-Path $bld) { Remove-Item $bld -Recurse -Force }
$dirs = @("$bld\gen","$bld\classes","$bld\dex","$bld\res","$bld\jni","$bld\apk","$bld\lib\arm64-v8a")
foreach ($d in $dirs) { New-Item -ItemType Directory -Path $d -Force | Out-Null }

# ---------- 1. native ----------
Step "编译 libmobctrl.so (arm64-v8a)"

$cppDir = Join-Path $mod "cpp"
$sources = Get-ChildItem $cppDir -Filter "*.cpp" | Select-Object -ExpandProperty FullName
Ok "源文件: $($sources.Count)"

$soOut = "$bld\lib\arm64-v8a\libmobctrl.so"
$ndkSysroot = Join-Path $ndk.FullName "toolchains\llvm\prebuilt\windows-x86_64\sysroot"

$clangArgs = @(
    "-shared","-fPIC","-O2","-std=c++17",
    "-fvisibility=hidden",
    "-Wl,--build-id=none","-Wl,-z,max-page-size=16384",
    "-DANDROID",
    "--sysroot=$ndkSysroot",
    "-I$cppDir",
    "-o", $soOut
) + $sources + @("-llog","-ldl","-landroid","-static-libstdc++")

& $clang @clangArgs
if ($LASTEXITCODE -ne 0) { Die "native 编译失败" }
Ok "libmobctrl.so $([math]::Round((Get-Item $soOut).Length/1KB,1)) KB"

# 导出符号检查
$dump = Join-Path $ndk.FullName "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe"
if (Test-Path $dump) {
    $syms = & $dump -D --defined-only $soOut 2>$null
    $jni = $syms | Select-String "Java_com_yg_mobctrl"
    Ok "JNI 导出 $($jni.Count) 个"
    if ($jni.Count -lt 5) { Die "JNI 符号不全，检查 -fvisibility=hidden 与 entry.cpp" }
}

# ---------- 2. 资源 ----------
Step "aapt2 编译资源"

$resDir = Join-Path $mod "res"
$resZip = "$bld\res\res.zip"
& $aapt2 compile --dir $resDir -o $resZip
if ($LASTEXITCODE -ne 0) { Die "aapt2 compile 失败" }

# 构建用 manifest（手动打包必须有 package 属性）
$mfSrc = Join-Path $mod "AndroidManifest.xml"
$mfBuild = "$bld\AndroidManifest.xml"
$mf = Get-Content -Raw -Encoding UTF8 $mfSrc
if ($mf -notmatch 'package\s*=') {
    $mf = $mf -replace '<manifest\s+xmlns:android="http://schemas\.android\.com/apk/res/android"',
                       '<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.yg.mobctrl"'
}
Set-Content -Path $mfBuild -Value $mf -Encoding UTF8
Ok "manifest 就绪"

$baseApk = "$bld\apk\base.apk"
& $aapt2 link -o $baseApk -I $ANDROID_JAR --manifest $mfBuild --java "$bld\gen" `
    --min-sdk-version $MinSdk --target-sdk-version $TargetSdk `
    --version-code 1 --version-name "1.0" $resZip
if ($LASTEXITCODE -ne 0) { Die "aapt2 link 失败" }
Ok "base.apk + R.java"

# ---------- 3. Java ----------
Step "javac 编译 Java"

$srcFiles = @()
$srcFiles += Get-ChildItem (Join-Path $mod "java") -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName
$srcFiles += Get-ChildItem (Join-Path $root "stubs") -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName
$srcFiles += Get-ChildItem "$bld\gen" -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName
Ok "源文件 $($srcFiles.Count) 个"

$argFile = "$bld\javac.args"
$srcFiles | ForEach-Object { '"' + ($_ -replace '\\','\\') + '"' } | Set-Content $argFile -Encoding UTF8

& $javac -encoding UTF-8 -source 8 -target 8 -nowarn `
    -bootclasspath $ANDROID_JAR -cp $ANDROID_JAR `
    -d "$bld\classes" "@$argFile" 2>&1 | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) { Die "javac 失败" }
Ok "class 编译完成"

# ---------- 4. dex ----------
Step "d8 生成 classes.dex"

# 只打包模块自身，stub 绝不进 dex
$modClasses = Get-ChildItem "$bld\classes\com\yg\mobctrl" -Recurse -Filter "*.class" -ErrorAction SilentlyContinue |
              Select-Object -ExpandProperty FullName
if (-not $modClasses) { Die "没找到 com.yg.mobctrl 的 class" }
Ok "参与 dex 的 class: $($modClasses.Count)"

& $d8 --lib $ANDROID_JAR --min-api $MinSdk --release --output "$bld\dex" @modClasses
if ($LASTEXITCODE -ne 0) { Die "d8 失败" }
Ok "classes.dex $([math]::Round((Get-Item "$bld\dex\classes.dex").Length/1KB,1)) KB"

# ---------- 5. 组装 ----------
Step "组装 APK"

Copy-Item $baseApk "$bld\unsigned.apk" -Force

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$zip = [System.IO.Compression.ZipFile]::Open("$bld\unsigned.apk", 'Update')
function AddEntry($zip, $entryName, $filePath) {
    $e = $zip.GetEntry($entryName)
    if ($e) { $e.Delete() }
    $new = $zip.CreateEntry($entryName, [System.IO.Compression.CompressionLevel]::Optimal)
    $s = $new.Open()
    $bytes = [System.IO.File]::ReadAllBytes($filePath)
    $s.Write($bytes, 0, $bytes.Length)
    $s.Close()
}

AddEntry $zip "classes.dex" "$bld\dex\classes.dex"
AddEntry $zip "lib/arm64-v8a/libmobctrl.so" $soOut

$xp = Join-Path $mod "assets\xposed_init"
if (Test-Path $xp) { AddEntry $zip "assets/xposed_init" $xp }
$zip.Dispose()
Ok "dex + so + xposed_init 已写入"

# ---------- 6. 对齐 + 签名 ----------
Step "zipalign + 签名"

& $zipalign -f -p 4 "$bld\unsigned.apk" "$bld\aligned.apk"
if ($LASTEXITCODE -ne 0) { Die "zipalign 失败" }

$ks = Join-Path $root "mobctrl.keystore"
if (-not (Test-Path $ks)) {
    & $keytool -genkeypair -v -keystore $ks -alias mobctrl -keyalg RSA -keysize 2048 `
        -validity 36500 -storepass mobctrl -keypass mobctrl `
        -dname "CN=MobCtrl, OU=YG, O=YG, L=CN, C=CN" 2>&1 | Out-Null
    Ok "生成 keystore"
}

$finalApk = "$bld\MobCtrl-v1.0.apk"
& $apksigner sign --ks $ks --ks-key-alias mobctrl `
    --ks-pass pass:mobctrl --key-pass pass:mobctrl `
    --min-sdk-version $MinSdk --out $finalApk "$bld\aligned.apk"
if ($LASTEXITCODE -ne 0) { Die "apksigner 失败" }

& $apksigner verify --print-certs $finalApk 2>&1 | Select-Object -First 6 | ForEach-Object { Write-Host "  $_" }

Write-Host ""
Ok "APK => $finalApk ($([math]::Round((Get-Item $finalApk).Length/1KB,1)) KB)"
Get-ChildItem $finalApk | Select-Object FullName, Length, LastWriteTime | Format-List
