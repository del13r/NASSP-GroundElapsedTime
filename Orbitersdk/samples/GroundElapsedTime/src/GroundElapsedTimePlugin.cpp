/***************************************************************************
  GroundElapsedTime named-pipe plugin entry points.

  It starts the GroundElapsedTime named-pipe exporter and registers the
  "GET Delay" MFD page.
  ***************************************************************************/

// Makes OrbiterAPI.h export ModuleDate() so Orbiter can recognise and log the
// plugin module. Define it in this one file only.
#define ORBITER_MODULE
#include "Orbitersdk.h"
#include "GroundElapsedTimeExport.h"
#include "GroundElapsedTimeMFD.h"
#include "GroundElapsedTimePause.h"

class GroundElapsedTimeModule : public oapi::Module
{
public:
	explicit GroundElapsedTimeModule(HINSTANCE hDLL) : oapi::Module(hDLL) {}

	void clbkSimulationStart(RenderMode) override
	{
		GroundElapsedTimeExportInit();
		GroundElapsedTimePauseInit(oapiGetPause());
	}

	void clbkSimulationEnd() override
	{
		GroundElapsedTimePauseExit();
		GroundElapsedTimeExportExit();
	}

	void clbkPreStep(double simt, double, double) override
	{
		GroundElapsedTimeExportStep(simt);
	}

	void clbkPause(bool paused) override
	{
		GroundElapsedTimePauseChanged(paused);
	}
};

static GroundElapsedTimeModule *g_module = NULL;

DLLCLBK void opcDLLInit(HINSTANCE hDLL)
{
	g_module = new GroundElapsedTimeModule(hDLL);
	oapiRegisterModule(g_module);
	GroundElapsedTimeMFDInit(hDLL);
}

DLLCLBK void opcDLLExit(HINSTANCE hDLL)
{
	GroundElapsedTimeMFDExit();
	delete g_module;
	g_module = NULL;
}

