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

## GET Delay MFD page (offset the published GET)

The plugin also registers an Orbiter MFD mode named **GET Delay**. Select it
like any MFD mode (MFD `MNU` / mode button, then pick *GET Delay*). It lets you
shift the GET published as **line 1** of the pipe, e.g. to line a text-to-speech
script up with a delayed broadcast. Line 2 (acceleration) and the two-line
format are unchanged, and with no offset applied the output is exactly the raw
GET as before.

**Sign convention** (whole seconds, `[+|-]M:SS`): the offset is *added*
to the simulator's raw GET: output = raw GET + offset. If the simulator GET is 15 s
ahead of the transcript, the offset is `+0:15` and the output is raw GET + 15 s;
`-0:15` subtracts 15 s. Unsupported
vessels still publish `0:00:00` (no offset applied).

Buttons (side buttons; keyboard shortcut in brackets):

| Button | Action |
| --- | --- |
| SIM [S] | Mark when the simulation event occurs, using a real-time wall-clock timestamp |
| TRN [T] | Mark when the corresponding event is heard in the transcript, using a real-time wall-clock timestamp |
| SET [M] | Type a signed **absolute** offset, e.g. `+2:30`, `-0:45`, `90` (seconds) in Orbiter's input box; APL replaces the applied offset with it |
| NEG [N] | Flip the staged value's sign (not available once applied) |
| APL [A] | **Apply** the staged value: a SIM/TRN correction is **added** to the applied offset; a SET value replaces it |
| CLR [C] | Remove the applied offset, both event marks and the candidate |

Once both events are marked, the measured **correction** is the transcript event timestamp
minus the simulation event timestamp, rounded to the nearest whole second.
Thus, if the transcript event is heard after the simulation event, the
correction is positive; if it is heard before the simulation event, it is\nnegative. Pressing either mark button again replaces that event's timestamp and\nrecalculates the correction once both marks exist; this never commits it.

**SIM/TRN corrections are cumulative.** APL *adds* the correction to the\ncurrently applied offset (e.g. applied `+2:30` plus correction `-0:20` gives\n`+2:10`), then clears both event marks so the next measurement starts fresh,\nand a second APL does nothing. **SET is absolute:** APL replaces the applied\noffset with the typed value. **CLR** is the only way back to zero. The page\nshows each event mark, the *measured correction* (or manual absolute value), the\n*resulting* / *new applied* offset, and whether it is `APPLIED` or\n`NOT APPLIED`. Offsets are limited to 99:59:59 (results are clamped). The\noffset lives in memory only: it is not saved with scenarios and resets when\nOrbiter restarts.

## Build (x64 only, self-contained)

Only the 64-bit build for OpenOrbiter 2024 + NASSP 9 is supported (CMake). The
earlier 32-bit Orbiter beta90 / Visual Studio solution path has been removed.
Everything is resolved inside this repository; nothing is read from a sibling
NASSP or Orbiter folder and nothing is written outside the repo.

### Prerequisites

- Windows with Visual Studio and the **Desktop development with C++**
  workload (includes CMake).
- Git on your `PATH`.
- An OpenOrbiter 2024 (x64) install, for its SDK headers and libraries.
### Download dependencies and build

The CMake build uses the current head of NASSP's `Orbiter2016`
branch by default. Setup prints the resolved commit and updates the repo-local
`.deps\NASSP-9` sparse checkout on each run. The Orbiter 2024 SDK is copied
from a local install.

From the repository root in a Developer PowerShell for Visual Studio, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup-deps-x64.ps1 -OrbiterSdkSource D:\Orbiter2024
cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build-x64 --config Release
```

Use `-NasspRef <branch-or-tag-or-commit>` with the setup script to select a
specific NASSP ref for a controlled test. The built plugin is written to
`build-output-x64\Modules\Plugin\GroundElapsedTime.dll`.

**Moving-target compatibility warning:** the NASSP binaries you run must match
the headers used to build this DLL closely. The current `Orbiter2016` branch
head can change and may break compilation, ABI compatibility, or runtime
behavior. You accept that risk when testing the moving branch; a successful
compile does not guarantee runtime compatibility.

### Install into Orbiter (separate step)

Building does not touch any Orbiter installation. Copy
`build-output-x64\Modules\Plugin\GroundElapsedTime.dll` into your OpenOrbiter 2024
`Modules\Plugin\` folder and enable **GroundElapsedTime** in the launchpad's
*Modules* tab. Keep only one copy of the DLL. The pipe is served while Orbiter runs.

## Files

- `src\GroundElapsedTimeCommon.cpp/.h` - shared NASSP mission-time lookup and
  formatting
- `src\GroundElapsedTimeExport.cpp/.h` - named-pipe server (worker thread)
- `Read-GroundElapsedTime.ps1` - sample pipe client
- `..\..\..\CMakeLists.txt`, `..\..\..\setup-deps-x64.ps1` - x64 build and
  dependency setup at the repository root
- `..\..\..\tests\pipe_harness` - pipe/offset protocol test

This repository does not include the Orbiter SDK, NASSP source, compiled
libraries, or generated build outputs.
