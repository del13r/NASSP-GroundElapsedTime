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

## Build requirements

- NASSP's Orbiter 2016 source tree and Project Apollo headers
- Visual Studio with the v141 x86/x64 C++ build tools
- The matching 32-bit Orbiter SDK
- A Win32/Release build

Set `OrbiterSdkDir` near the top of
`Build\VC2017\GroundElapsedTime.vcxproj` to your local x86 SDK folder.
Do not use the x64 SDK for this build.

Open the NASSP solution, select **Release | Win32**, and build the
`GroundElapsedTime` project. The resulting DLL is written to the configured
`Modules\Plugin` output folder. Place it in the 32-bit Orbiter installation
that runs NASSP.

## Files

- `src\GroundElapsedTimeCommon.cpp/.h` - shared NASSP mission-time lookup and
  formatting
- `src\GroundElapsedTimeExport.cpp/.h` - once-per-simulation-second file
  output
- `Build\VC2017\GroundElapsedTime.vcxproj` - Visual Studio project

This repository does not include the Orbiter SDK, NASSP source, compiled
libraries, or generated build outputs.
