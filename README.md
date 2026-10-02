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

