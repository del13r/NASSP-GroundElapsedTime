/***************************************************************************
  This file is part of the GroundElapsedTime MFD addon for NASSP.

  This header declares one small, shared helper function used by the named-pipe
  exporter (GroundElapsedTimeExport.cpp) to answer the same question:
  "for this vessel, what is the current Ground Elapsed Time, in seconds?"

  Keeping this logic in exactly one place means the pipe value follows exactly the NASSP rules below.
  ***************************************************************************/

#ifndef __GROUNDELAPSEDTIMECOMMON_H
#define __GROUNDELAPSEDTIMECOMMON_H

class VESSEL;

// Candidate sources discovered for Ground Elapsed Time calculation.
struct GroundElapsedTimeSources
{
	bool focusSupported;
	bool focusIsSivb;
	double focusTime;

	bool hasMcc;
	double mccTime;

	bool hasSaturn;
	double saturnTime;

	bool hasLem;
	double lemTime;

	bool hasCrawler;
	double crawlerTime;

	bool hasSivb;
	double sivbTime;
};

// Returns true if t is a finite, initialized mission time (rejects NASSP's MINUS_INFINITY = -1.0e9).
inline bool IsValidMissionTime(double t)
{
	return (t > -1.0e8 && t < 1.0e9);
}

// Pure precedence resolver: calculates Ground Elapsed Time from candidate sources.
// When focus is supported, preserves NASSP's direct vessel/MCC precedence (including
// S-IVB special behavior). When focus is unsupported (e.g. an EVA astronaut), follows
// fallback precedence:
//   1. MCC mission time when positive.
//   2. Otherwise available Saturn / Crawler / LEM mission-time source(s).
//   3. If an uncrewed S-IVB is the only/best supported fallback, use its own mission time.
// Returns true with outSeconds populated, or false if no appropriate source exists.
inline bool ResolveGroundElapsedTime(const GroundElapsedTimeSources &sources, double &outSeconds)
{
	if (sources.focusSupported)
	{
		if (sources.focusIsSivb)
		{
			if (IsValidMissionTime(sources.focusTime))
			{
				outSeconds = sources.focusTime;
				return true;
			}
			return false;
		}

		double mt = 0.0;
		bool haveMt = false;
		if (sources.hasMcc && IsValidMissionTime(sources.mccTime) && sources.mccTime > 0.0)
		{
			mt = sources.mccTime;
			haveMt = true;
		}
		else if (IsValidMissionTime(sources.focusTime))
		{
			mt = sources.focusTime;
			haveMt = true;
		}

		if (haveMt)
		{
			outSeconds = mt;
			return true;
		}
		return false;
	}

	// Focus is unsupported: fallback precedence
	// 1. MCC mission time when positive
	if (sources.hasMcc && IsValidMissionTime(sources.mccTime) && sources.mccTime > 0.0)
	{
		outSeconds = sources.mccTime;
		return true;
	}

	// 2. Otherwise available Saturn / Crawler / LEM mission-time source(s)
	// Prefer active in-flight positive mission times (> 0.0)
	if (sources.hasLem && IsValidMissionTime(sources.lemTime) && sources.lemTime > 0.0)
	{
		outSeconds = sources.lemTime;
		return true;
	}
	if (sources.hasSaturn && IsValidMissionTime(sources.saturnTime) && sources.saturnTime > 0.0)
	{
		outSeconds = sources.saturnTime;
		return true;
	}
	if (sources.hasCrawler && IsValidMissionTime(sources.crawlerTime) && sources.crawlerTime > 0.0)
	{
		outSeconds = sources.crawlerTime;
		return true;
	}

	// If no positive mission time, check for valid non-positive times (e.g. pre-liftoff countdown)
	if (sources.hasSaturn && IsValidMissionTime(sources.saturnTime))
	{
		outSeconds = sources.saturnTime;
		return true;
	}
	if (sources.hasCrawler && IsValidMissionTime(sources.crawlerTime))
	{
		outSeconds = sources.crawlerTime;
		return true;
	}
	if (sources.hasLem && IsValidMissionTime(sources.lemTime))
	{
		outSeconds = sources.lemTime;
		return true;
	}

	// 3. If an uncrewed S-IVB is the only/best supported fallback, use its own mission time
	if (sources.hasSivb && IsValidMissionTime(sources.sivbTime))
	{
		outSeconds = sources.sivbTime;
		return true;
	}

	// Unsupported remains when no appropriate supported spacecraft/MCC source exists
	return false;
}

// ComputeGroundElapsedTime looks at "vessel" (any vessel Orbiter knows
// about - it might be a Saturn, a LEM, an S-IVB stage, a Crawler, an EVA
// astronaut, or something NASSP doesn't recognize at all) and works out
// Ground Elapsed Time in seconds.
//
// When "vessel" is a supported NASSP spacecraft, it follows the exact
// rules of ProjectApolloMFD:
//   1. If the vessel is an S-IVB third stage, use its own mission time
//      directly (it isn't a crewed vessel, so there's no reason to prefer
//      Mission Control's clock for it).
//   2. Otherwise, prefer the simulated Mission Control ("MCC") vessel's
//      mission time, if one exists in the scenario and its time is > 0.
//   3. If MCC's time looks like nonsense (<= 0, which happens before
//      liftoff), fall back to whichever of Saturn/Crawler/LEM the vessel
//      actually is.
//
// When "vessel" is unsupported (e.g. focus switched to an EVA astronaut
// such as Pete Conrad), it safely searches the active scenario for
// supported NASSP vehicles and uses fallback precedence:
//   1. MCC mission time when positive.
//   2. Otherwise available Saturn/Crawler/LEM mission-time source(s).
//   3. If an uncrewed S-IVB is the only/best supported fallback, use its own mission time.
//
// Returns true if a valid mission time source was found, writing that time
// (in seconds - can be negative before liftoff) into "outSeconds".
// Returns false (leaving outSeconds untouched) when no appropriate supported
// source exists.
bool ComputeGroundElapsedTime(VESSEL *vessel, double &outSeconds);

// Formats mission time as H:MM:SS (truncated to whole seconds, negative
// countdown sign preserved) into a null-terminated string.
void FormatGroundElapsedTimeHMS(double totalSeconds, char *buffer, int bufferSize);

#endif // !__GROUNDELAPSEDTIMECOMMON_H


