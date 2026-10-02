# GroundElapsedTime addon

This addon reads NASSP's Project Apollo mission-time logic and publishes the
current Ground Elapsed Time (GET) and Orbiter's time acceleration on a local
Windows named pipe, for another program (transcript tool, script, overlay) to
read while a NASSP simulation is running. It no longer writes any file.

## Pipe interface

- **Pipe name:** `\\.\pipe\GroundElapsedTime` (local machine only; remote
  clients are rejected). The plugin is the server; your program is the client.
- **Payload:** each update is one complete pipe message (a single write on a
  message-mode pipe) containing exactly two ASCII lines, each ended by `\n`:

```text
3:15:24
10
```

  1. GET as `H:MM:SS` (hours are not capped at 24; a countdown such as five
     seconds before liftoff is `-0:00:05`).
  2. Current Orbiter time acceleration (`oapiGetTimeAcceleration`), in plain
     decimal with `.` as separator and no trailing zeros: `1`, `10`, `0.5`.

- **Source of GET:** the focused vessel, using NASSP's logic: S-IVB time is
  preferred when focused; otherwise MCC time is used, with Saturn, Crawler and
  LEM as pre-liftoff fallbacks. Unsupported vessels give `0:00:00`.
- **Cadence:** a new message is sent immediately whenever either line changes
  (including time acceleration changes), and otherwise every 100 ms of real
  (wall-clock) time (10 Hz). A client that connects receives the latest value
  immediately. GET is formatted to whole seconds, so 10 Hz republishes provide
  fresh delivery without increasing the value's one-second resolution. The
  simulation-step callback drives this cadence: when Orbiter runs below 10
  steps per second, publication is limited by its step rate.
- **Never blocks Orbiter:** the simulation thread only hands the value to a
  background thread, which does all pipe I/O asynchronously.

### Lifecycle and reconnect

- The pipe appears when Orbiter loads the plugin and is removed when it exits.
  If it does not exist yet, keep retrying the connection.
- One client at a time. If you disconnect (or stop reading for more than
  about 1 second so the write stalls), the plugin drops that connection and
  waits for the next client. If Orbiter exits, your read ends/fails; reconnect
  in a loop. Read continuously: a message is one `Read` in message mode.
- If another program already owns the pipe name, the plugin retries every
  second and does not disturb it.

### Try it (PowerShell)

With Orbiter running a scenario, run from this folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\Read-GroundElapsedTime.ps1
```

It prints lines such as `GET=3:15:24  acceleration=10x`. The essence in your
own code is: connect a `NamedPipeClientStream('.', 'GroundElapsedTime', 'In')`, `Read` into a buffer, split the ASCII text on `\n`, and use the **last two lines**. A byte-mode reader (the .NET default; a read-only client cannot switch to message mode) may receive several queued updates joined together, and the newest is last.

## Build (self-contained, no absolute paths)

Everything is resolved inside this repository. Nothing is read from a
sibling NASSP or Orbiter folder, and nothing is written outside the repo.

### 1. Prerequisites

- Windows with Visual Studio (2019/2022/2026) and the **Desktop development
  with C++** workload, plus the **MSVC v145 x86/x64 build tools** component
  (the project uses the v145 toolset).
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
| Orbiter beta90 SDK headers and 32-bit libs | copied from your local install via `-OrbiterSdkSource` | no download; hashes in `sdk.manifest` |

Results: `.deps\NASSP\` and `.deps\OrbiterSDK\Orbitersdk\{include,lib}`.
The NASSP source is downloaded, never committed. Re-run with `-Force` to re-clone it.

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
launchpad's *Modules* tab. A correctly loaded plugin logs a line such as
`Module GroundElapsedTime.dll .. [Build ..., API 190914]` in `Orbiter.log`.
Keep only one copy: delete any stale `Modules\GroundElapsedTime.dll` (root of
`Modules`) so it cannot be confused with `Modules\Plugin\GroundElapsedTime.dll`. The pipe is served while Orbiter runs.

## Files

- `src\GroundElapsedTimeCommon.cpp/.h` - shared NASSP mission-time lookup and
  formatting
- `src\GroundElapsedTimeExport.cpp/.h` - named-pipe server (worker thread)
- `Read-GroundElapsedTime.ps1` - sample pipe client
- `Build\VC2017\GroundElapsedTime.vcxproj` - Visual Studio project
- `..\..\..\GroundElapsedTime.sln`, `..\..\..\setup-deps.ps1` - solution and
  dependency setup at the repository root

This repository does not include the Orbiter SDK, NASSP source, compiled
libraries, or generated build outputs.




