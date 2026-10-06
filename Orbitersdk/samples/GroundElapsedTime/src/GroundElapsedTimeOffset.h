/***************************************************************************
  GET offset ("delay") state, shared by the pipe exporter and the MFD page.

  Sign convention (whole seconds): a POSITIVE offset is ADDED to the published GET
  (+0:15 -> output is 15s ahead of raw simulator GET); a NEGATIVE offset
  subtracts. Typical use: simulator GET is 15 s ahead of the transcript, so
  the output for the transcript is raw GET + 15 s.
  Output GET = raw GET + offset. Offset 0 leaves GET exactly untouched.

  Two kinds of candidate exist:
    - correction (from SIM/TRN event marks): ADDED to the applied offset on Apply
    - absolute (manual SET entry): REPLACES the applied offset on Apply

  This file has no Orbiter dependencies, so the test harness can use it.
  All functions are meant to be called from Orbiter's main thread only.
  ***************************************************************************/

#ifndef __GROUNDELAPSEDTIMEOFFSET_H
#define __GROUNDELAPSEDTIMEOFFSET_H

#include <windows.h>

// Largest accepted offset magnitude: 99:59:59 expressed in seconds.
const long GET_OFFSET_MAX_SECONDS = 359999;

// Parses "[+|-]M:SS" or "[+|-]SS" (whole seconds; SS must be < 60 when a
// minutes part is present; whitespace around the text is ignored).
// Returns false and leaves outSeconds untouched on any invalid input.
bool GetOffsetParse(const char *text, long &outSeconds);

// Formats as "+M:SS" / "-M:SS" (minutes are not capped; zero is "+0:00").
void GetOffsetFormat(long seconds, char *buffer, int bufferSize);

// Returns rawSeconds plus the currently applied offset.
double GetOffsetApplyToGET(double rawSeconds);

long GetOffsetApplied();
bool GetOffsetHasCandidate();
long GetOffsetCandidate();
// True if the candidate has been committed with GetOffsetApplyCandidate().
bool GetOffsetCandidateApplied();
// True if the candidate is a SIM/TRN correction added to the applied offset.
bool GetOffsetCandidateIsCorrection();
// Offset that Apply would produce (or did produce, once applied).
long GetOffsetResulting();

// Capture or replace the respective event timestamp using wall-clock time.
// Once both timestamps exist, each mark (re)calculates an unapplied candidate.
void GetOffsetMarkSimEvent(ULONGLONG timestampMs);
void GetOffsetMarkTranscriptEvent(ULONGLONG timestampMs);
bool GetOffsetHasSimEvent();
bool GetOffsetHasTranscriptEvent();

// Sets the candidate directly (manual entry). Clamped to +-GET_OFFSET_MAX_SECONDS.
void GetOffsetSetCandidate(long seconds);
// Negates the candidate (+ <-> -). False if there is none.
bool GetOffsetFlipCandidate();
// Commits the candidate: a correction is added to the applied offset (result
// clamped), an absolute candidate replaces it. After a correction is applied the
// event marks are cleared so the next measurement starts fresh. False if there
// is no candidate or it was already applied.
bool GetOffsetApplyCandidate();
// Removes the applied offset, both event marks and any candidate.
void GetOffsetReset();

#endif
