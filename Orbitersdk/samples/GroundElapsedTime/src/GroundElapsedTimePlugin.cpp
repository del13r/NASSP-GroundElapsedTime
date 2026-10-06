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

DLLCLBK void opcDLLInit(HINSTANCE hDLL)
{
	GroundElapsedTimeExportInit();
	GroundElapsedTimeMFDInit(hDLL);
}

DLLCLBK void opcDLLExit(HINSTANCE hDLL)
{
	GroundElapsedTimeMFDExit();
	GroundElapsedTimeExportExit();
}

DLLCLBK void opcPreStep(double simt, double simdt, double mjd)
{
	GroundElapsedTimeExportStep(simt);
}

