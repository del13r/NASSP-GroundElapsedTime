#include "Orbitersdk.h"
#include <stdio.h>

extern "C" void opcDLLInit(HINSTANCE);
extern "C" void opcDLLExit(HINSTANCE);
static oapi::Module *module;
static int starts, ends, pauses, steps, mfdStarts, mfdEnds;
static bool initialPaused, lastPaused;
static double initialSampleTime;
static int httpStarts, httpEnds, httpPauses, initialSamples;
void oapiRegisterModule(oapi::Module *m) { module = m; }
bool oapiGetPause() { return initialPaused; }
double oapiGetSimTime() { return 123.0; }
void GroundElapsedTimeExportInit() { ++starts; }
void GroundElapsedTimeExportExit() { ++ends; }
void GroundElapsedTimeExportStep(double) { ++steps; }
void GroundElapsedTimeExportHttpSample(double simt) { ++initialSamples; initialSampleTime = simt; }
void GroundElapsedTimeHttpInit(bool) { ++httpStarts; }
void GroundElapsedTimeHttpExit() { ++httpEnds; }
void GroundElapsedTimeHttpPause(bool) { ++httpPauses; }
int GroundElapsedTimeHttpTakeError() { return 0; }
void oapiWriteLogV(const char *, ...) {}
void GroundElapsedTimePauseInit(bool paused) { lastPaused = paused; }
void GroundElapsedTimePauseChanged(bool paused) { ++pauses; lastPaused = paused; }
void GroundElapsedTimePauseExit() { ++ends; }
void GroundElapsedTimeMFDInit(HINSTANCE) { ++mfdStarts; }
void GroundElapsedTimeMFDExit() { ++mfdEnds; }

int main()
{
	opcDLLInit(NULL);
	if (!module || starts || ends || httpStarts || mfdStarts != 1) return 1;
	initialPaused = true;
	module->clbkSimulationStart(oapi::Module::RENDER_NONE);
	if (starts != 1 || httpStarts != 1 || initialSamples != 1 || initialSampleTime != 123.0 || !lastPaused) return 2;
	module->clbkPause(false);
	module->clbkPreStep(1, 1, 1);
	if (pauses != 1 || httpPauses != 1 || lastPaused || steps != 1) return 3;
	module->clbkSimulationEnd();
	if (ends != 2 || httpEnds != 1) return 4;
	initialPaused = false;
	module->clbkSimulationStart(oapi::Module::RENDER_WINDOW);
	if (starts != 2 || httpStarts != 2 || initialSamples != 2 || lastPaused) return 5;
	module->clbkSimulationEnd();
	if (ends != 4 || httpEnds != 2) return 6;
	opcDLLExit(NULL);
	if (ends != 4 || mfdEnds != 1) return 7;
	opcDLLInit(NULL);
	opcDLLExit(NULL);
	if (starts != 2 || ends != 4 || mfdStarts != 2 || mfdEnds != 2) return 8;
	printf("Lifecycle: PASSED\n");
	return 0;
}
