<#
  Downloads/copies the x64 build dependencies into the repo-local, git-ignored .deps folder.
  Run once:
    powershell -ExecutionPolicy Bypass -File .\setup-deps-x64.ps1 -OrbiterSdkSource D:\Orbiter2024
  Nothing is installed outside this repository. (The Win32/beta90 build keeps
  using setup-deps.ps1 and .deps\NASSP, .deps\OrbiterSDK - untouched.)
#>
[CmdletBinding()]
param(
    [string]$NasspRef = 'Orbiter2016',
    # Your OpenOrbiter 2024 x64 install folder (the one containing Orbiter.exe
    # and Orbitersdk\). Only include\, lib\ and XRSound\ are copied.
    [string]$OrbiterSdkSource
)

$ErrorActionPreference = 'Stop'

# --- NASSP source -----------------------------------------------------------
$NasspRepo   = 'https://github.com/rcflyinghokie/NASSP.git'
# -----------------------------------------------------------------------------

function Invoke-Git {
    param([string[]]$Arguments)

    $output = & git @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "git $($Arguments -join ' ') failed (exit $exitCode): $($output -join [Environment]::NewLine)"
    }
    return ($output -join [Environment]::NewLine)
}

$root = $PSScriptRoot
$deps = Join-Path $root '.deps'
$nassp = Join-Path $deps 'NASSP-9'
$sdkRoot = Join-Path $deps 'OrbiterSDK-2024'
New-Item -ItemType Directory -Force $deps | Out-Null

$NasspRef = $NasspRef.Trim()
if (-not $NasspRef -or $NasspRef.StartsWith('-') -or $NasspRef.Contains("`n") -or $NasspRef.Contains("`r")) {
    throw 'NasspRef must be a branch, tag, or commit (not an option or empty value)'
}

if (Test-Path $nassp) {
    if (-not (Test-Path (Join-Path $nassp '.git'))) {
        throw "$nassp exists but is not a Git checkout; move it aside before running setup"
    }
    $topLevel = Invoke-Git -Arguments @('-C', $nassp, 'rev-parse', '--show-toplevel')
    if ([IO.Path]::GetFullPath($topLevel.Trim()) -ne [IO.Path]::GetFullPath($nassp)) {
        throw "$nassp is not the root of its Git checkout"
    }
    $origin = Invoke-Git -Arguments @('-C', $nassp, 'remote', 'get-url', 'origin')
    if ($origin.TrimEnd('/') -ne $NasspRepo) {
        throw "Unexpected NASSP checkout origin '$origin'; expected '$NasspRepo'"
    }
    Write-Host 'Updating existing repo-local NASSP checkout ...'
}
else {
    Write-Host 'Cloning NASSP source ...'
    Invoke-Git -Arguments @('clone', '--quiet', '--filter=blob:none', '--no-checkout', $NasspRepo, $nassp) | Out-Null
}

Invoke-Git -Arguments @('-C', $nassp, 'sparse-checkout', 'set', '--no-cone', '/Orbitersdk/samples/ProjectApollo/') | Out-Null

$remoteBranch = Invoke-Git -Arguments @('ls-remote', '--heads', $NasspRepo, "refs/heads/$NasspRef")
if ($remoteBranch.Trim()) {
    Invoke-Git -Arguments @('-C', $nassp, 'fetch', '--quiet', 'origin', "+refs/heads/$NasspRef`:refs/remotes/origin/$NasspRef") | Out-Null
    $expectedHead = Invoke-Git -Arguments @('-C', $nassp, 'rev-parse', "refs/remotes/origin/$NasspRef")
    Invoke-Git -Arguments @('-C', $nassp, 'checkout', '--quiet', '-B', $NasspRef, "refs/remotes/origin/$NasspRef") | Out-Null
    Invoke-Git -Arguments @('-C', $nassp, 'reset', '--hard', "refs/remotes/origin/$NasspRef") | Out-Null
    $checkedOutBranch = Invoke-Git -Arguments @('-C', $nassp, 'symbolic-ref', '--short', 'HEAD')
    if ($checkedOutBranch -ne $NasspRef) {
        throw "NASSP checkout is on branch '$checkedOutBranch', expected '$NasspRef'"
    }
}
else {
    Invoke-Git -Arguments @('-C', $nassp, 'fetch', '--quiet', 'origin', $NasspRef) | Out-Null
    $expectedHead = Invoke-Git -Arguments @('-C', $nassp, 'rev-parse', '--verify', 'FETCH_HEAD^{commit}')
    Invoke-Git -Arguments @('-C', $nassp, 'checkout', '--quiet', '--detach', '--force', $expectedHead) | Out-Null
    Invoke-Git -Arguments @('-C', $nassp, 'reset', '--hard', $expectedHead) | Out-Null
    $checkedOutBranch = 'detached'
}
Invoke-Git -Arguments @('-C', $nassp, 'clean', '-fd') | Out-Null
$head = Invoke-Git -Arguments @('-C', $nassp, 'rev-parse', 'HEAD')
if ($head -ne $expectedHead) { throw "NASSP is at $head, expected fetched ref $expectedHead" }
$workingTree = Invoke-Git -Arguments @('-C', $nassp, 'status', '--porcelain')
if ($workingTree.Trim()) { throw "NASSP checkout is not clean after reset:`n$workingTree" }
Write-Host ''
Write-Host "NASSP SOURCE RESOLVED: $NasspRef -> $head (checkout: $checkedOutBranch)" -ForegroundColor Cyan
Write-Host ''

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
