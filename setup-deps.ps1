<#
  Downloads the build dependencies into the repo-local, git-ignored .deps folder.
  Run once:  powershell -ExecutionPolicy Bypass -File .\setup-deps.ps1
  Nothing is installed outside this repository.
#>
[CmdletBinding()]
param(
    [switch]$Force,
    # Path to a matching Orbiter beta90 (API 190914) 32-bit Orbitersdk folder.
    # Only needed the first time; it is copied into .deps\OrbiterSDK.
    [string]$OrbiterSdkSource
)

$ErrorActionPreference = 'Stop'

# --- Pinned dependency versions ---------------------------------------------
$NasspRepo   = 'https://github.com/orbiternassp/NASSP.git'
$NasspTag    = 'NASSP-V8.0-Beta-Orbiter2016-2641'
$NasspCommit = '606aa359ebdf4c920336871f90f742035ec65304'

# -----------------------------------------------------------------------------

$root = $PSScriptRoot
$deps = Join-Path $root '.deps'
$nassp = Join-Path $deps 'NASSP'
$sdkRoot = Join-Path $deps 'OrbiterSDK'
New-Item -ItemType Directory -Force $deps | Out-Null

# --- NASSP source (only the ProjectApollo sources are checked out) ----------
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
Write-Host "NASSP source OK ($NasspTag, $NasspCommit)"

# --- Orbiter SDK (copied from a local Orbiter beta90 install, never downloaded)
# The addon must be built against the same SDK as the Orbiter that loads it
# (beta90, API 190914). Orbiter 2024 SDK headers/libs are NOT compatible.
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
    foreach ($d in 'include','lib') {
        Copy-Item -Recurse -Force (Join-Path $src $d) (Join-Path $sdk $d)
    }
    $lines = foreach ($p in $needed) {
        '{0}  {1}' -f (Get-FileHash (Join-Path $sdk $p) -Algorithm SHA256).Hash, $p
    }
    Set-Content $manifest $lines
}
foreach ($p in $needed) {
    if (-not (Test-Path (Join-Path $sdk $p))) {
        throw "Orbiter SDK not found in .deps. Re-run with -OrbiterSdkSource <path to your Orbiter beta90 Orbitersdk folder>"
    }
}
Write-Host "Orbiter SDK OK (copied from local install). SHA-256 manifest:"
Get-Content $manifest | ForEach-Object { Write-Host "  $_" }
Write-Host "Done. Open GroundElapsedTime.sln and build Release | Win32."

