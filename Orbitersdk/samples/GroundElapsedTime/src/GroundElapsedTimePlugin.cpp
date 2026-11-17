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
#include "GroundElapsedTimeHttp.h"

class GroundElapsedTimeModule : public oapi::Module
{
public:
	explicit GroundElapsedTimeModule(HINSTANCE hDLL) : oapi::Module(hDLL) {}

	void clbkSimulationStart(RenderMode) override
	{
		GroundElapsedTimeHttpInit(oapiGetPause());
		GroundElapsedTimeExportInit();
		GroundElapsedTimePauseInit(oapiGetPause());
		GroundElapsedTimeExportHttpSample(oapiGetSimTime());
	}

	void clbkSimulationEnd() override
	{
		GroundElapsedTimeHttpExit();
		GroundElapsedTimePauseExit();
		GroundElapsedTimeExportExit();
	}

	void clbkPreStep(double simt, double, double) override
	{
		ReportHttpError();
		GroundElapsedTimeExportStep(simt);
	}

	void clbkPause(bool paused) override
	{
		ReportHttpError();
		GroundElapsedTimeHttpPause(paused);
		GroundElapsedTimePauseChanged(paused);
	}

private:
	void ReportHttpError()
	{
		int error = GroundElapsedTimeHttpTakeError();
		if (error)
			oapiWriteLogV("GroundElapsedTime HTTP: server error %d; check port 8765, retrying where possible", error);
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

