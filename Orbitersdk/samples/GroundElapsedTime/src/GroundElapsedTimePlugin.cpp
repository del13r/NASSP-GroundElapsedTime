/***************************************************************************
  GroundElapsedTime file-export plugin entry points.

  This file deliberately registers no visible MFD mode. It only starts the
  once-per-simulation-second GroundElapsedTime.txt exporter.
  ***************************************************************************/

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
