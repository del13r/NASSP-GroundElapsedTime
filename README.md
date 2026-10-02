# NASSP-GroundElapsedTime

Addon that exports NASSP Project Apollo Ground Elapsed Time to
`GroundElapsedTime.txt`.

Quick start (details in
`Orbitersdk/samples/GroundElapsedTime/README.md`):

1. `powershell -ExecutionPolicy Bypass -File .\setup-deps.ps1` (downloads
   NASSP source and the Orbiter SDK into the ignored `.deps\` folder)
2. Open `GroundElapsedTime.sln`, choose **Release | x86**, build.
3. Copy `build-output\Modules\Plugin\GroundElapsedTime.dll` into your Orbiter
   installation's `Modules\Plugin\` folder and enable it in the launchpad.
