<#
  Downloads the build dependencies into the repo-local, git-ignored .deps folder.
  Run once:  powershell -ExecutionPolicy Bypass -File .\setup-deps.ps1
  Nothing is installed outside this repository.
#>
[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'

# --- Pinned dependency versions ---------------------------------------------
$NasspRepo   = 'https://github.com/orbiternassp/NASSP.git'
$NasspTag    = 'NASSP-V8.0-Beta-Orbiter2016-2641'
$NasspCommit = '606aa359ebdf4c920336871f90f742035ec65304'

$OrbiterUrl    = 'https://github.com/orbitersim/orbiter/releases/download/2024/Orbiter-x86.zip'
$OrbiterSha256 = '5475F83EC66F0653198A7404CC58933EB10F2C2F50FE8F6C1EB69799D1765DD0'
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

# --- Orbiter SDK (headers + import libs from the official x86 release) ------
$sdk = Join-Path $sdkRoot 'Orbitersdk'
$sdkOk = (Test-Path (Join-Path $sdk 'include\Orbitersdk.h')) -and
         (Test-Path (Join-Path $sdk 'lib\Orbiter.lib')) -and
         (Test-Path (Join-Path $sdkRoot 'sdk.sha256'))
if ($Force -or -not $sdkOk) {
    $zip = Join-Path $deps 'Orbiter-x86.zip'
    if (-not (Test-Path $zip) -or
        (Get-FileHash $zip -Algorithm SHA256).Hash -ne $OrbiterSha256) {
        Write-Host "Downloading Orbiter 2024 x86 (~360 MB) ..."
        Invoke-WebRequest -Uri $OrbiterUrl -OutFile $zip -UseBasicParsing
    }
    $actual = (Get-FileHash $zip -Algorithm SHA256).Hash
    if ($actual -ne $OrbiterSha256) {
        Remove-Item $zip
        throw "Orbiter zip hash mismatch: $actual (expected $OrbiterSha256)"
    }
    if (Test-Path $sdkRoot) { Remove-Item -Recurse -Force $sdkRoot }
    New-Item -ItemType Directory -Force $sdkRoot | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $z = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach ($e in $z.Entries) {
            if ($e.FullName -notmatch '^Orbitersdk/(include|lib|XRSound)/' -or
                $e.FullName.EndsWith('/')) { continue }
            $dest = Join-Path $sdkRoot $e.FullName
            New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $true)
        }
    } finally { $z.Dispose() }
    Set-Content (Join-Path $sdkRoot 'sdk.sha256') $OrbiterSha256
    Remove-Item $zip
}
foreach ($p in 'include\Orbitersdk.h','lib\Orbiter.lib','lib\Orbitersdk.lib') {
    if (-not (Test-Path (Join-Path $sdk $p))) { throw "Missing $p in Orbiter SDK" }
}
Write-Host "Orbiter SDK OK (Orbiter 2024 x86)"
Write-Host "Done. Open GroundElapsedTime.sln and build Release | Win32."

