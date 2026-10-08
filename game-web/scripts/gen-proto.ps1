# Generate TypeScript from ../proto using protoc + ts-proto.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Repo = Split-Path -Parent $Root
$ProtoDir = Join-Path $Repo "proto"
$OutDir = Join-Path $Root "src\gen"
$Plugin = Join-Path $Root "node_modules\.bin\protoc-gen-ts_proto.cmd"

if (-not (Test-Path $Plugin)) {
  $Plugin = Join-Path $Root "node_modules\.bin\protoc-gen-ts_proto"
}
if (-not (Test-Path $Plugin)) {
  throw "protoc-gen-ts_proto not found; run npm install in game-web"
}

$ProtocCmd = Get-Command protoc -ErrorAction SilentlyContinue
$Protoc = if ($ProtocCmd) { $ProtocCmd.Source } else { $null }
if (-not $Protoc) {
  $VcpkgProtoc = "D:\work\source\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe"
  if (Test-Path $VcpkgProtoc) { $Protoc = $VcpkgProtoc }
}
if (-not $Protoc) { throw "protoc not found on PATH" }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-ChildItem $OutDir -File -ErrorAction SilentlyContinue | Remove-Item -Force

$ProtoFiles = Get-ChildItem $ProtoDir -Filter "*.proto" | ForEach-Object { $_.FullName }
$Opts = @(
  "esModuleInterop=true",
  "forceLong=number",
  "snakeToCamel=false",
  "env=browser",
  "useOptionals=messages",
  "outputServices=false",
  "exportCommonSymbols=false",
  "unrecognizedEnum=false"
) -join ","

Write-Host "==> protoc: $Protoc"
Write-Host "==> out: $OutDir"
& $Protoc `
  --plugin="protoc-gen-ts_proto=$Plugin" `
  --ts_proto_out="$OutDir" `
  --ts_proto_opt="$Opts" `
  -I "$ProtoDir" `
  @ProtoFiles

if ($LASTEXITCODE -ne 0) { throw "protoc failed: $LASTEXITCODE" }
Write-Host "==> generated:" (Get-ChildItem $OutDir -Filter "*.ts").Name
