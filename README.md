# NASSP-GroundElapsedTime

Addon that exports NASSP Project Apollo Ground Elapsed Time and Orbiter time acceleration on the local named pipe `\\.\pipe\GroundElapsedTime`.
Event-driven: a message is published for every displayed GET second crossed (in order, also across 60x frames and backward steps), immediately on acceleration changes, plus a 1 s heartbeat.
A **GET Delay** MFD page can shift the published GET by a signed offset (output = raw GET + offset). Mark the simulation event and when it is heard in the transcript to calculate a correction that APL adds to the current offset (SET enters an absolute offset) (see the addon README).

## Build (x64 only: OpenOrbiter 2024 + NASSP 9) - CMake

This is the only supported build; the old 32-bit Orbiter beta90 build has been
removed. Details are in `Orbitersdk/samples/GroundElapsedTime/README.md`. By default,
the x64 build uses the current head of NASSP's `Orbiter2016` branch and prints
the resolved commit during dependency setup. You can select another NASSP
branch, tag, or commit with `-NasspRef` for a controlled test.

You need: Visual Studio with the "Desktop development with C++" workload
(includes CMake), Git, and your OpenOrbiter 2024 folder (e.g. `D:\Orbiter2024`).

1. Open "Developer PowerShell for VS" and `cd` to this repository's folder.
2. Fetch the current NASSP `Orbiter2016` branch head and copy the Orbiter 2024
   SDK from your install into the ignored `.deps\` folder:

       powershell -ExecutionPolicy Bypass -File .\setup-deps-x64.ps1 -OrbiterSdkSource D:\Orbiter2024

   To test a specific branch, tag, or commit instead, add
   `-NasspRef <branch-or-tag-or-commit>`. Re-running setup updates the existing
   repo-local NASSP checkout and resets it to the selected ref.

3. Configure and build (change the generator name if your Visual Studio is
   different, e.g. `"Visual Studio 17 2022"`):

       cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64
       cmake --build build-x64 --config Release

   Result: `build-output-x64\Modules\Plugin\GroundElapsedTime.dll`
   (64-bit; also exports `GetModuleVersion`, required by Orbiter 2024).
4. Copy it to `D:\Orbiter2024\Modules\Plugin\` and enable `GroundElapsedTime`
   in the launchpad's Modules tab. The pipe name, message format and
   event-driven cadence are as documented in the addon README.

**Moving-target compatibility warning:** the NASSP binaries you run must match
the headers used to build this DLL closely. Testing against the current
`Orbiter2016` branch head is a moving-target experiment: NASSP changes can
break compilation, ABI compatibility, or runtime behavior. You accept that
risk when using this build; a successful compile does not guarantee runtime
compatibility.

Optional protocol test (no Orbiter needed; uses the real exporter code with a
stubbed Orbiter API):

    cmake -S . -B build-x64 -DGET_BUILD_PIPE_HARNESS=ON
    cmake --build build-x64 --config Release --target pipe_harness
    .\build-x64\harness\Release\pipe_harness.exe

Status: builds and passes the stub pipe test; not yet tested inside a running
OpenOrbiter 2024 / NASSP 9 session.
