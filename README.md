# NASSP-GroundElapsedTime

Addon that exports NASSP Project Apollo Ground Elapsed Time and Orbiter time acceleration on the local named pipe `\\.\pipe\GroundElapsedTime`.
Values are republished at 10 Hz (100 ms), or immediately when they change.

Quick start (details in
`Orbitersdk/samples/GroundElapsedTime/README.md`):

1. `powershell -ExecutionPolicy Bypass -File .\setup-deps.ps1` (downloads
   NASSP source and the Orbiter SDK into the ignored `.deps\` folder)
2. Open `GroundElapsedTime.sln`, choose **Release | x86**, build.
3. Copy `build-output\Modules\Plugin\GroundElapsedTime.dll` into your Orbiter
   installation's `Modules\Plugin\` folder and enable it in the launchpad.


## 64-bit build (OpenOrbiter 2024 + NASSP 9) - CMake

This is a separate build path from the Win32 beta90 solution above, which is
unchanged. Use it if you run OpenOrbiter 2024 (x64) with NASSP 9
(`NASSP-V9.0-Folgers-Alpha-421`). The DLL is built against NASSP 9's headers,
so it must be used with a NASSP 9 install of that same version.

You need: Visual Studio with the "Desktop development with C++" workload
(includes CMake), Git, and your OpenOrbiter 2024 folder (e.g. `D:\Orbiter2024`).

1. Open "Developer PowerShell for VS" and `cd` to this repository's folder.
2. Download dependencies (NASSP 9 pinned to commit
   `8d3e3e3ee99ece8cb88cb6aaf3f04af80efc3a6c`; the Orbiter 2024 SDK is copied
   from your install) into the ignored `.deps\` folder:

       powershell -ExecutionPolicy Bypass -File .\setup-deps-x64.ps1 -OrbiterSdkSource D:\Orbiter2024

3. Configure and build (change the generator name if your Visual Studio is
   different, e.g. `"Visual Studio 17 2022"`):

       cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64
       cmake --build build-x64 --config Release

   Result: `build-output-x64\Modules\Plugin\GroundElapsedTime.dll`
   (64-bit; also exports `GetModuleVersion`, required by Orbiter 2024).
4. Copy it to `D:\Orbiter2024\Modules\Plugin\` and enable `GroundElapsedTime`
   in the launchpad's Modules tab. The pipe name, message format and 10 Hz
   cadence are identical to the Win32 build.

Optional protocol test (no Orbiter needed; uses the real exporter code with a
stubbed Orbiter API):

    cmake -S . -B build-x64 -DGET_BUILD_PIPE_HARNESS=ON
    cmake --build build-x64 --config Release --target pipe_harness
    .\build-x64\harness\Release\pipe_harness.exe

Status: builds and passes the stub pipe test; not yet tested inside a running
OpenOrbiter 2024 / NASSP 9 session.
