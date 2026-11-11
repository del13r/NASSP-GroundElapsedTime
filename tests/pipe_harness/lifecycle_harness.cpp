#include "Orbitersdk.h"
#include <stdio.h>

extern "C" void opcDLLInit(HINSTANCE);
extern "C" void opcDLLExit(HINSTANCE);
static oapi::Module *module;
static int starts, ends, pauses, steps, mfdStarts, mfdEnds;
static bool initialPaused, lastPaused;
void oapiRegisterModule(oapi::Module *m) { module = m; }
bool oapiGetPause() { return initialPaused; }
void GroundElapsedTimeExportInit() { ++starts; }
void GroundElapsedTimeExportExit() { ++ends; }
void GroundElapsedTimeExportStep(double) { ++steps; }
void GroundElapsedTimePauseInit(bool paused) { lastPaused = paused; }
void GroundElapsedTimePauseChanged(bool paused) { ++pauses; lastPaused = paused; }
void GroundElapsedTimePauseExit() { ++ends; }
void GroundElapsedTimeMFDInit(HINSTANCE) { ++mfdStarts; }
void GroundElapsedTimeMFDExit() { ++mfdEnds; }

int main()
{
	opcDLLInit(NULL);
	if (!module || starts || ends || mfdStarts != 1) return 1;
	initialPaused = true;
	module->clbkSimulationStart(oapi::Module::RENDER_NONE);
	if (starts != 1 || !lastPaused) return 2;
	module->clbkPause(false);
	module->clbkPreStep(1, 1, 1);
	if (pauses != 1 || lastPaused || steps != 1) return 3;
	module->clbkSimulationEnd();
	if (ends != 2) return 4;
	initialPaused = false;
	module->clbkSimulationStart(oapi::Module::RENDER_WINDOW);
	if (starts != 2 || lastPaused) return 5;
	module->clbkSimulationEnd();
	if (ends != 4) return 6;
	opcDLLExit(NULL);
	if (ends != 4 || mfdEnds != 1) return 7;
	opcDLLInit(NULL);
	opcDLLExit(NULL);
	if (starts != 2 || ends != 4 || mfdStarts != 2 || mfdEnds != 2) return 8;
	printf("Lifecycle: PASSED\n");
	return 0;
}
