/***************************************************************************
  GroundElapsedTime named-pipe plugin entry points.

  This file deliberately registers no visible MFD mode. It only starts the
  GroundElapsedTime named-pipe exporter.
  ***************************************************************************/

// Required: makes OrbiterAPI.h export ModuleDate(), which Orbiter beta90 uses
// to recognise and log a plugin module. Define it in this one file only.
#define ORBITER_MODULE
#include "Orbitersdk.h"
#include "GroundElapsedTimeExport.h"

DLLCLBK void opcDLLInit(HINSTANCE hDLL)
{
	GroundElapsedTimeExportInit();
}

DLLCLBK void opcDLLExit(HINSTANCE hDLL)
{
	GroundElapsedTimeExportExit();
}

DLLCLBK void opcPreStep(double simt, double simdt, double mjd)
{
	GroundElapsedTimeExportStep(simt);
}

