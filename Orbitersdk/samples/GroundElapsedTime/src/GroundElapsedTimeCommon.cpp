/***************************************************************************
  This file is part of the GroundElapsedTime MFD addon for NASSP.

  See GroundElapsedTimeCommon.h for what ComputeGroundElapsedTime() does
  and why it exists as a shared helper.

  Beginner's note on why so many #include lines are needed below: Saturn,
  LEM, SIVB, Crawler and MCC are big, "real" NASSP classes with lots of
  systems (electrical, guidance computer, switches, etc.), and their own
  header files (saturn.h, LEM.h, ...) need all of *their* sub-system
  headers included first so the compiler knows the full shape of the
  class. This exact same list of includes, in this order, is what NASSP's
  own ProjectApolloMFD.cpp uses for the same reason - we're simply copying
  a combination that is already proven to compile correctly inside NASSP.

  Importantly, we do NOT need to link against Saturn.lib, LEM.lib, etc.
  Saturn::GetMissionTime()/LEM::GetMissionTime()/Crawler::GetMissionTime()
  are all declared directly inside their header files with their full
  body (called an "inline" function), so the compiler can use them
  straight from the header. MCC::GetMissionTime() and SIVB::GetMissionTime()
  are declared as "virtual" instead - that means the *actual* running
  Saturn.dll/LEM.dll/MCC.dll/SIVb.dll (already loaded into Orbiter's
  process by the time our MFD runs) supplies the real code to run, and
  C++ finds it automatically through the vessel object's "vtable" (a
  hidden table of function pointers every polymorphic C++ object carries).
  This is exactly the same trick ProjectApolloMFD.vcxproj relies on to
  read Saturn/LEM/etc. mission times without linking their .lib files.
  ***************************************************************************/

// To force Orbitersdk.h to use <fstream> in any compiler version
#pragma include_alias( <fstream.h>, <fstream> )
#include "Orbitersdk.h"

#include "math.h"
#include "stdio.h"
#include "windows.h"
#include "nasspsound.h"
#include "soundlib.h"
#include "tracer.h"
#include "nasspdefs.h"
#include "toggleswitch.h"
#include "apolloguidance.h"
#include "CSMcomputer.h"
#include "LEMcomputer.h"
#include "IMU.h"
#include "saturn.h"
#include "LEM.h"
#include "Crawler.h"
#include "MCCVessel.h"
#include "MCC.h"
#include "sivb.h"
#include "nassputils.h"

#include "GroundElapsedTimeCommon.h"

using namespace nassp;

bool ComputeGroundElapsedTime(VESSEL *vessel, double &outSeconds)
{
	GroundElapsedTimeSources sources = {};

	// Work out what kind of NASSP vessel we've been given, exactly the way
	// ProjectApolloMFD's constructor does it: by comparing the vessel's
	// Orbiter class name (utils::IsVessel checks VESSEL::GetClassName()
	// against NASSP's known class names).
	Saturn *focusSaturn = NULL;
	LEM *focusLem = NULL;
	SIVB *focusSivb = NULL;
	Crawler *focusCrawler = NULL;

	if (vessel)
	{
		if (utils::IsVessel(vessel, utils::Saturn))
		{
			focusSaturn = (Saturn *)vessel;
			sources.focusSupported = true;
			sources.focusTime = focusSaturn->GetMissionTime();
		}
		else if (utils::IsVessel(vessel, utils::LEM))
		{
			focusLem = (LEM *)vessel;
			sources.focusSupported = true;
			sources.focusTime = focusLem->GetMissionTime();
		}
		else if (utils::IsVessel(vessel, utils::SIVB))
		{
			focusSivb = (SIVB *)vessel;
			sources.focusSupported = true;
			sources.focusIsSivb = true;
			sources.focusTime = focusSivb->GetMissionTime();
		}
		else if (utils::IsVessel(vessel, utils::Crawler))
		{
			focusCrawler = (Crawler *)vessel;
			sources.focusSupported = true;
			sources.focusTime = focusCrawler->GetMissionTime();
		}
	}

	// Look for the simulated Mission Control vessel, exactly the way
	// ProjectApolloMFD's constructor does: by its fixed scenario name
	// "MCC". Not every scenario includes an MCC vessel, so this can
	// legitimately stay NULL.
	MCC *mcc = NULL;
	OBJHANDLE hMCC = oapiGetVesselByName("MCC");
	if (hMCC != NULL)
	{
		VESSEL *pVessel = oapiGetVesselInterface(hMCC);
		if (pVessel && utils::IsVessel(pVessel, utils::MCC))
		{
			MCCVessel *pMCCVessel = static_cast<MCCVessel *>(pVessel);
			if (pMCCVessel->mcc)
				mcc = pMCCVessel->mcc;
		}
	}

	// If the focused vessel is supported, preserve existing behavior
	if (sources.focusSupported)
	{
		if (mcc)
		{
			sources.hasMcc = true;
			sources.mccTime = mcc->GetMissionTime();
		}
		return ResolveGroundElapsedTime(sources, outSeconds);
	}

	// Focus is unsupported (e.g. an EVA astronaut): search the active Orbiter scenario
	// for supported NASSP vehicle(s) and compute mission time from an appropriate
	// supported source, without ever casting an unsupported vessel.
	Saturn *fallbackSaturn = NULL;
	LEM *fallbackLem = NULL;
	Crawler *fallbackCrawler = NULL;
	SIVB *fallbackSivb = NULL;

	DWORD vesselCount = oapiGetVesselCount();
	for (DWORD i = 0; i < vesselCount; ++i)
	{
		OBJHANDLE hV = oapiGetVesselByIndex((int)i);
		if (!hV)
			continue;
		VESSEL *v = oapiGetVesselInterface(hV);
		if (!v)
			continue;

		if (utils::IsVessel(v, utils::Saturn))
		{
			Saturn *s = (Saturn *)v;
			if (!fallbackSaturn || (fallbackSaturn->GetMissionTime() <= 0.0 && s->GetMissionTime() > 0.0))
				fallbackSaturn = s;
		}
		else if (utils::IsVessel(v, utils::LEM))
		{
			LEM *l = (LEM *)v;
			if (!fallbackLem || (fallbackLem->GetMissionTime() <= 0.0 && l->GetMissionTime() > 0.0))
				fallbackLem = l;
		}
		else if (utils::IsVessel(v, utils::Crawler))
		{
			Crawler *c = (Crawler *)v;
			if (!fallbackCrawler)
				fallbackCrawler = c;
		}
		else if (utils::IsVessel(v, utils::SIVB))
		{
			SIVB *s = (SIVB *)v;
			if (!fallbackSivb || (fallbackSivb->GetMissionTime() <= 0.0 && s->GetMissionTime() > 0.0))
				fallbackSivb = s;
		}
		else if (!mcc && utils::IsVessel(v, utils::MCC))
		{
			MCCVessel *pMCCVessel = static_cast<MCCVessel *>(v);
			if (pMCCVessel && pMCCVessel->mcc)
				mcc = pMCCVessel->mcc;
		}
	}

	if (mcc)
	{
		sources.hasMcc = true;
		sources.mccTime = mcc->GetMissionTime();
	}
	if (fallbackSaturn)
	{
		sources.hasSaturn = true;
		sources.saturnTime = fallbackSaturn->GetMissionTime();
	}
	if (fallbackLem)
	{
		sources.hasLem = true;
		sources.lemTime = fallbackLem->GetMissionTime();
	}
	if (fallbackCrawler)
	{
		sources.hasCrawler = true;
		sources.crawlerTime = fallbackCrawler->GetMissionTime();
	}
	if (fallbackSivb)
	{
		sources.hasSivb = true;
		sources.sivbTime = fallbackSivb->GetMissionTime();
	}

	return ResolveGroundElapsedTime(sources, outSeconds);
}

void FormatGroundElapsedTimeHMS(double totalSeconds, char *buffer, int bufferSize)
{
	long seconds = (long)totalSeconds;
	long magnitude = (seconds < 0) ? -seconds : seconds;
	long hours = magnitude / 3600;
	long minutes = (magnitude / 60) % 60;
	long remainder = magnitude % 60;

	if (seconds < 0)
		sprintf(buffer, "-%ld:%02ld:%02ld", hours, minutes, remainder);
	else
		sprintf(buffer, "%ld:%02ld:%02ld", hours, minutes, remainder);

	if (bufferSize > 0)
		buffer[bufferSize - 1] = '\0';
}

