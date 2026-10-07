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
// I/O. Enqueues one message per displayed whole second crossed since the
// previous step (in order, either direction), an immediate message for the
// first sample / acceleration change / discontinuity, and a heartbeat message
// once per second of wall-clock time when nothing else was published.
void GroundElapsedTimeExportStep(double simt);

// Number of queued messages dropped because the bounded queue was full
// (diagnostics / tests).
unsigned long GroundElapsedTimeExportDroppedCount();

#endif // !__GROUNDELAPSEDTIMEEXPORT_H
