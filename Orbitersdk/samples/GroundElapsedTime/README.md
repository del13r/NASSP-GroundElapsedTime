# GroundElapsedTime addon

This addon reads NASSP's Project Apollo mission-time logic and writes the
current Ground Elapsed Time (GET) to `GroundElapsedTime.txt` in the Orbiter
installation folder. It is intended for another program, script, or
transcript tool to read while a NASSP simulation is running.

The file contains two lines generated from the same signed mission-time
value:

```text
3:15:24
00 03 15 24
```

The first line uses `H:MM:SS`. The second uses `DD HH MM SS`, with days
increasing after each 24 hours. A five-second countdown is written as:

```text
-0:00:05
-00 00 00 05
```

The time source follows NASSP's Project Apollo logic: S-IVB time is preferred
when focused; otherwise MCC time is used, with Saturn, Crawler, and LEM as
pre-liftoff fallbacks. Unsupported vessels produce two zero lines.

## Build (self-contained, no absolute paths)

Everything is resolved inside this repository. Nothing is read from a
sibling NASSP or Orbiter folder, and nothing is written outside the repo.

### 1. Prerequisites

- Windows with Visual Studio (2019/2022/2026) and the **Desktop development
  with C++** workload, plus the **MSVC v141 (VS 2017) x86/x64 build tools**
  component (the project uses the v141 toolset).
- Git on your `PATH`.
- About 1 GB of free disk space for the downloads.

### 2. Download the dependencies (once)

In PowerShell from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup-deps.ps1
```

This populates the git-ignored `.deps\` folder with pinned versions:

| Dependency | Source | Pin |
| --- | --- | --- |
| NASSP source (`Orbitersdk\samples\ProjectApollo`, needed because the addon uses NASSP's internal vessel classes) | `https://github.com/orbiternassp/NASSP.git` | tag `NASSP-V8.0-Beta-Orbiter2016-2641`, commit `606aa359ebdf4c920336871f90f742035ec65304` |
| Orbiter SDK headers and 32-bit libs | `https://github.com/orbitersim/orbiter/releases/download/2024/Orbiter-x86.zip` | Orbiter 2024 release, SHA-256 `5475F83EC66F0653198A7404CC58933EB10F2C2F50FE8F6C1EB69799D1765DD0` |

Results: `.deps\NASSP\` and `.deps\OrbiterSDK\Orbitersdk\{include,lib,XRSound}`.
These files are downloaded, never committed. To change versions, edit the
pins at the top of `setup-deps.ps1`. Re-run with `-Force` to re-download.

### 3. Build

1. Open `GroundElapsedTime.sln` (repository root) in Visual Studio.
2. Select **Release** and **x86**.
3. **Build > Build Solution**.

Or from a Developer PowerShell: `msbuild GroundElapsedTime.sln /p:Configuration=Release /p:Platform=x86`.

Output: `build-output\Modules\Plugin\GroundElapsedTime.dll`
(intermediates are in `build-output\obj\`). Do not use the x64 SDK.

### 4. Install into Orbiter (separate step)

Building does not touch any Orbiter installation. Orbiter and NASSP must
already be installed (32-bit Orbiter running NASSP). Copy
`build-output\Modules\Plugin\GroundElapsedTime.dll` into that installation's
`Modules\Plugin\` folder, then enable **GroundElapsedTime** in the Orbiter
launchpad's *Modules* tab. `GroundElapsedTime.txt` is written to the Orbiter
installation folder while a simulation runs.

## Files

- `src\GroundElapsedTimeCommon.cpp/.h` - shared NASSP mission-time lookup and
  formatting
- `src\GroundElapsedTimeExport.cpp/.h` - once-per-simulation-second file
  output
- `Build\VC2017\GroundElapsedTime.vcxproj` - Visual Studio project
- `..\..\..\GroundElapsedTime.sln`, `..\..\..\setup-deps.ps1` - solution and
  dependency setup at the repository root

This repository does not include the Orbiter SDK, NASSP source, compiled
libraries, or generated build outputs.