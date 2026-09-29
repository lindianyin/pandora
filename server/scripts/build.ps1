# Build pandora-server (MSVC / CMake + vcpkg).
# Usage (from repo root or anywhere):
#   pwsh -File server/scripts/build.ps1
#   pwsh -File server/scripts/build.ps1 -Config Debug
#   pwsh -File server/scripts/build.ps1 -Configure
#   pwsh -File server/scripts/build.ps1 -Clean
param(
  [ValidateSet("Release", "Debug", "RelWithDebInfo", "MinSizeRel")]
  [string]$Config = "Release",
  [switch]$Configure,
  [switch]$Clean,
  [string]$Target = "pandora-server",
  [string]$Generator = "Visual Studio 17 2022",
  [string]$Arch = "x64",
  [string]$VcpkgRoot = ""
)

$ErrorActionPreference = "Stop"

$ServerRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepoRoot = (Resolve-Path (Join-Path $ServerRoot "..")).Path
$BuildDir = Join-Path $ServerRoot "build"
$ExePath = Join-Path $BuildDir "$Config\pandora-server.exe"

function Resolve-VcpkgRoot {
  param([string]$Hint)
  $candidates = @()
  if ($Hint) { $candidates += $Hint }
  if ($env:VCPKG_ROOT) { $candidates += $env:VCPKG_ROOT }
  $candidates += "D:\work\source\vcpkg"
  $candidates += (Join-Path $RepoRoot "vcpkg")
  foreach ($c in $candidates) {
    if (-not $c) { continue }
    $toolchain = Join-Path $c "scripts\buildsystems\vcpkg.cmake"
    if (Test-Path $toolchain) { return (Resolve-Path $c).Path }
  }
  return $null
}

$VcpkgRoot = Resolve-VcpkgRoot -Hint $VcpkgRoot
if (-not $VcpkgRoot) {
  Write-Error "vcpkg not found. Set VCPKG_ROOT or pass -VcpkgRoot <path> (expected scripts/buildsystems/vcpkg.cmake)."
  exit 1
}
$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

Write-Host "==> server root: $ServerRoot"
Write-Host "==> config: $Config  target: $Target"
Write-Host "==> vcpkg: $VcpkgRoot"

function Stop-PandoraServer {
  $procs = Get-Process -Name "pandora-server" -ErrorAction SilentlyContinue
  if (-not $procs) { return }
  Write-Host "==> stopping running pandora-server (avoid LNK1104)..."
  $procs | Stop-Process -Force
  Start-Sleep -Seconds 1
}

if ($Clean) {
  if (Test-Path $BuildDir) {
    Stop-PandoraServer
    Write-Host "==> removing $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
  }
}

$needConfigure = $Configure -or -not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))
if (-not $needConfigure -and (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
  $cache = Get-Content (Join-Path $BuildDir "CMakeCache.txt") -Raw
  # Failed configure without toolchain leaves packages NOTFOUND / TOOLCHAIN UNINITIALIZED.
  if ($cache -match 'nlohmann_json_DIR:PATH=nlohmann_json_DIR-NOTFOUND' -or
      $cache -match 'CMAKE_TOOLCHAIN_FILE:UNINITIALIZED') {
    Write-Host "==> stale/broken CMake cache detected; reconfigure with vcpkg"
    $needConfigure = $true
    Stop-PandoraServer
    Remove-Item -Recurse -Force $BuildDir
  }
}

if ($needConfigure) {
  # Forward slashes: cmake/vcpkg are happier with them on Windows.
  $ToolchainUnix = $Toolchain -replace '\\', '/'
  $ManifestUnix = $RepoRoot -replace '\\', '/'
  Write-Host "==> cmake configure ($Generator $Arch)"
  Write-Host "==> toolchain: $ToolchainUnix"
  Push-Location $ServerRoot
  try {
    cmake -B build -G $Generator -A $Arch `
      "-DCMAKE_TOOLCHAIN_FILE=$ToolchainUnix" `
      "-DVCPKG_TARGET_TRIPLET=x64-windows" `
      "-DVCPKG_MANIFEST_DIR=$ManifestUnix"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  } finally {
    Pop-Location
  }
}

Stop-PandoraServer

Write-Host "==> cmake --build build --config $Config --target $Target"
Push-Location $ServerRoot
try {
  cmake --build build --config $Config --target $Target
  if ($LASTEXITCODE -ne 0) {
    Write-Host "==> build failed; retry after stop pandora-server..."
    Stop-PandoraServer
    cmake --build build --config $Config --target $Target
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  }
} finally {
  Pop-Location
}

if (Test-Path $ExePath) {
  Write-Host "==> ok: $ExePath"
} else {
  Write-Host "==> build finished (exe path not found at $ExePath)"
}

exit 0
