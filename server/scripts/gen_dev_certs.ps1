# Generate self-signed PEM cert/key for local HTTPS/WSS.
$ErrorActionPreference = "Stop"
$outDir = Join-Path $PSScriptRoot "..\certs"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Push-Location (Join-Path $PSScriptRoot "gencert")
try {
  dotnet run -c Release -- "$outDir"
} finally {
  Pop-Location
}
