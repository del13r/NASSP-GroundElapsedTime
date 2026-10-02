/***************************************************************************
  GroundElapsedTime named-pipe exporter.

  Publishes the current focused-vessel Ground Elapsed Time and Orbiter time
  acceleration on the local named pipe \\.\pipe\GroundElapsedTime. Each
  update is one complete pipe message: two ASCII lines, each ended by '\n':

      3:15:24\n
      10\n

  See the addon README for lifecycle, cadence and client examples.
  ***************************************************************************/

#ifndef __GROUNDELAPSEDTIMEEXPORT_H
#define __GROUNDELAPSEDTIMEEXPORT_H

// Starts the background pipe server thread (called from opcDLLInit).
void GroundElapsedTimeExportInit();

// Stops the pipe server thread and releases all handles (opcDLLExit).
void GroundElapsedTimeExportExit();

// Called every simulation step from the Orbiter thread. Never blocks on pipe
// I/O; it only hands the latest message to the worker thread when it changes
// or at least once per 100 ms of wall-clock time.
void GroundElapsedTimeExportStep(double simt);

#endif // !__GROUNDELAPSEDTIMEEXPORT_H
