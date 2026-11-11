// Minimal stand-in for the Orbiter API, used only by the pipe test harness.
#pragma once
#include <windows.h>
class VESSEL {};
VESSEL *oapiGetFocusInterface();
double oapiGetTimeAcceleration();

#ifdef GET_TEST_MODULE_LIFECYCLE
#define DLLCLBK extern "C"
namespace oapi {
class Module {
public:
	enum RenderMode { RENDER_NONE, RENDER_WINDOW };
	explicit Module(HINSTANCE) {}
	virtual ~Module() {}
	virtual void clbkSimulationStart(RenderMode) {}
	virtual void clbkSimulationEnd() {}
	virtual void clbkPreStep(double, double, double) {}
	virtual void clbkPause(bool) {}
};
}
void oapiRegisterModule(oapi::Module *);
bool oapiGetPause();
#endif
