# 一键打包：Release 构建（静态链入 C++ 运行库 + 去符号表）并收集运行必需文件
# 用法：powershell -ExecutionPolicy Bypass -File .\package.ps1
#       -NoZip 只出目录不打压缩包
param(
    [string]$BuildDir = "cmake-build-dist",
    [string]$OutDir   = "dist",
    [switch]$NoZip
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)
$root = (Get-Location).Path

function Find-First($candidates) {
    foreach ($path in $candidates) {
        if ($path -and (Test-Path $path)) { return $path }
    }
    return $null
}

# cmake：优先 PATH，其次 CLion 自带
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $hits = @()
    foreach ($base in @("$env:ProgramFiles\JetBrains", "$env:LOCALAPPDATA\Programs", "D:\JetBrains", "C:\JetBrains")) {
        if (-not (Test-Path $base)) { continue }
        $hits += Get-ChildItem $base -Filter "CLion*" -Directory -ErrorAction SilentlyContinue |
                 ForEach-Object { Join-Path $_.FullName "bin\cmake\win\x64\bin\cmake.exe" }
    }
    $cmake = Find-First $hits
}
if (-not $cmake) { throw "找不到 cmake：请安装 CMake 或把它加入 PATH" }

# ninja 与 MinGW 跟着 cmake 一起找，避免和系统里其它 GCC 混用
# CLion 布局：<root>\bin\cmake\win\x64\bin\cmake.exe
$toolsRoot = $cmake -replace '\\bin\\cmake\\.*$', ''
$ninja = Find-First @("$toolsRoot\bin\ninja\win\x64\ninja.exe", (Get-Command ninja -ErrorAction SilentlyContinue).Source)
$gcc   = Find-First @("$toolsRoot\bin\mingw\bin\gcc.exe", (Get-Command gcc -ErrorAction SilentlyContinue).Source)

$configure = @("-S", $root, "-B", (Join-Path $root $BuildDir), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
if ($ninja) { $configure += "-DCMAKE_MAKE_PROGRAM=$ninja" }
if ($gcc) {
    $mingwBin = Split-Path $gcc
    $configure += "-DCMAKE_C_COMPILER=$mingwBin\gcc.exe", "-DCMAKE_CXX_COMPILER=$mingwBin\g++.exe"
}

Write-Host "配置..." -ForegroundColor Cyan
& $cmake @configure
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }

Write-Host "编译..." -ForegroundColor Cyan
& $cmake --build (Join-Path $root $BuildDir) --target mqtt_gui
if ($LASTEXITCODE -ne 0) { throw "编译失败" }

# 只收集 install() 声明的运行必需文件：exe + 中文字体 + winpthread
$outRoot = Join-Path $root $OutDir
$prefix  = Join-Path $outRoot "mqtt_gui"
if (Test-Path $outRoot) { Remove-Item $outRoot -Recurse -Force }
New-Item -ItemType Directory -Path $prefix -Force | Out-Null
& $cmake --install (Join-Path $root $BuildDir) --prefix $prefix
if ($LASTEXITCODE -ne 0) { throw "安装到 $OutDir 失败" }

$exe = Join-Path $prefix "mqtt_gui.exe"
$exeKB = [math]::Round((Get-Item $exe).Length / 1KB)
$totalKB = [math]::Round(((Get-ChildItem $prefix -Recurse -File | Measure-Object Length -Sum).Sum) / 1KB)
Write-Host ("exe {0} KB，整包 {1} KB" -f $exeKB, $totalKB) -ForegroundColor Green

if (-not $NoZip) {
    $zip = Join-Path $outRoot "mqtt_gui-$(Get-Date -Format yyyyMMdd).zip"
    Compress-Archive -Path (Join-Path $prefix "*") -DestinationPath $zip -Force
    Write-Host ("压缩包 {0}（{1} KB）" -f $zip, [math]::Round((Get-Item $zip).Length / 1KB)) -ForegroundColor Green
}
