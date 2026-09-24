# Pandora — vcpkg 国内源环境（当前 PowerShell 会话）
# 用法:  . .\scripts\vcpkg-cn-env.ps1
#
# 实测（2026-09）:
# - mirrors.tuna.tsinghua.edu.cn/git/vcpkg.git 与 /vcpkg 均 404（高校镜像已下线）
# - vcpkg-configuration.json 的 "asset-sources" 字段不被当前工具识别
#   资产镜像请用环境变量 X_VCPKG_ASSET_SOURCES
# - 可用：Gitee 同步仓 https://gitee.com/mirrors/vcpkg.git（见仓库根 vcpkg-configuration.json）

$ErrorActionPreference = "Continue"

$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } elseif (Test-Path "D:\work\source\vcpkg") { "D:\work\source\vcpkg" } else { $null }
$GiteeVcpkg = "https://gitee.com/mirrors/vcpkg.git"
if ($VcpkgRoot -and (Test-Path (Join-Path $VcpkgRoot ".git"))) {
  Push-Location $VcpkgRoot
  try {
    git remote set-url origin $GiteeVcpkg
    Write-Host "[ok] vcpkg origin -> $GiteeVcpkg"
  } finally { Pop-Location }
  if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = $VcpkgRoot }
}

$env:VCPKG_BINARY_SOURCES = "clear;default,readwrite"
[System.Environment]::SetEnvironmentVariable("VCPKG_BINARY_SOURCES", $env:VCPKG_BINARY_SOURCES, "User")
Write-Host "[ok] VCPKG_BINARY_SOURCES=$env:VCPKG_BINARY_SOURCES"

# 若日后清华资产站恢复，可取消注释：
# $env:X_VCPKG_ASSET_SOURCES = "clear;x-azurl,https://mirrors.tuna.tsinghua.edu.cn/vcpkg/,read"

Write-Host "Next: cd pandora; vcpkg install --triplet x64-windows"
