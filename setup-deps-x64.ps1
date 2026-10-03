<#
  Downloads/copies the x64 build dependencies into the repo-local, git-ignored .deps folder.
  Run once:
    powershell -ExecutionPolicy Bypass -File .\setup-deps-x64.ps1 -OrbiterSdkSource D:\Orbiter2024
  Nothing is installed outside this repository. (The Win32/beta90 build keeps
  using setup-deps.ps1 and .deps\NASSP, .deps\OrbiterSDK - untouched.)
#>
[CmdletBinding()]
param(
    [switch]$Force,
    # Your OpenOrbiter 2024 x64 install folder (the one containing Orbiter.exe
    # and Orbitersdk\). Only include\, lib\ and XRSound\ are copied.
    [string]$OrbiterSdkSource
)

$ErrorActionPreference = 'Stop'

# --- Pinned dependency versions ---------------------------------------------
$NasspRepo   = 'https://github.com/rcflyinghokie/NASSP.git'
$NasspTag    = 'NASSP-V9.0-Folgers-Alpha-421'
$NasspCommit = '8d3e3e3ee99ece8cb88cb6aaf3f04af80efc3a6c'
# -----------------------------------------------------------------------------

$root = $PSScriptRoot
$deps = Join-Path $root '.deps'
$nassp = Join-Path $deps 'NASSP-9'
$sdkRoot = Join-Path $deps 'OrbiterSDK-2024'
New-Item -ItemType Directory -Force $deps | Out-Null

$nasspOk = (Test-Path (Join-Path $nassp '.git')) -and
    ((git -C $nassp rev-parse HEAD 2>$null) -eq $NasspCommit)
if ($Force -or -not $nasspOk) {
    if (Test-Path $nassp) { Remove-Item -Recurse -Force $nassp }
    Write-Host "Cloning NASSP $NasspTag ..."
    git clone --quiet --filter=blob:none --no-checkout $NasspRepo $nassp
    if ($LASTEXITCODE) { throw 'git clone failed' }
    git -C $nassp sparse-checkout set --no-cone '/Orbitersdk/samples/ProjectApollo/'
    git -C $nassp checkout --quiet $NasspCommit
    if ($LASTEXITCODE) { throw 'git checkout failed' }
}
$head = git -C $nassp rev-parse HEAD
if ($head -ne $NasspCommit) { throw "NASSP is at $head, expected $NasspCommit" }
Write-Host "NASSP 9 source OK ($NasspTag, $NasspCommit)"

# --- OpenOrbiter 2024 SDK (copied from your local install, never downloaded) -
$sdk = Join-Path $sdkRoot 'Orbitersdk'
$manifest = Join-Path $sdkRoot 'sdk.manifest'
$needed = 'include\Orbitersdk.h','include\OrbiterAPI.h','lib\Orbiter.lib','lib\Orbitersdk.lib'
if ($OrbiterSdkSource) {
    $src = (Resolve-Path $OrbiterSdkSource).Path
    if (Test-Path (Join-Path $src 'Orbitersdk\include')) { $src = Join-Path $src 'Orbitersdk' }
    foreach ($p in $needed) {
        if (-not (Test-Path (Join-Path $src $p))) { throw "Missing $p in $src" }
    }
    if (Test-Path $sdkRoot) { Remove-Item -Recurse -Force $sdkRoot }
    New-Item -ItemType Directory -Force $sdk | Out-Null
    foreach ($d in 'include','lib','XRSound') {
        if (Test-Path (Join-Path $src $d)) {
            Copy-Item -Recurse -Force (Join-Path $src $d) (Join-Path $sdk $d)
        }
    }
    $lines = foreach ($p in $needed) {
        '{0}  {1}' -f (Get-FileHash (Join-Path $sdk $p) -Algorithm SHA256).Hash, $p
    }
    Set-Content $manifest $lines
}
foreach ($p in $needed) {
    if (-not (Test-Path (Join-Path $sdk $p))) {
        throw "OpenOrbiter SDK not found in .deps. Re-run with -OrbiterSdkSource <your Orbiter 2024 folder>"
    }
}
Write-Host "OpenOrbiter 2024 SDK OK (copied from local install). SHA-256 manifest:"
Get-Content $manifest | ForEach-Object { Write-Host "  $_" }
Write-Host "Done. Now build with build-x64.ps1 (see README)."
