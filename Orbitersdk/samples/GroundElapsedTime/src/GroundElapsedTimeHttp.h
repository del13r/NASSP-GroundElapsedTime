#pragma once

// All lifecycle and sample calls are made on Orbiter's simulation thread.
void GroundElapsedTimeHttpInit(bool paused);
void GroundElapsedTimeHttpExit();
void GroundElapsedTimeHttpPause(bool paused);
void GroundElapsedTimeHttpSample(double get, double accel, bool valid,
	const void *vessel, long offset, double simt);
int GroundElapsedTimeHttpTakeError();
