#include "GroundElapsedTimeOffset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static long g_applied = 0;
static bool g_hasCandidate = false;
static long g_candidate = 0;
static bool g_candidateApplied = false;
static bool g_candidateIsCorrection = false;
static bool g_hasSimEvent = false;
static bool g_hasTranscriptEvent = false;
static ULONGLONG g_simEventMs = 0;
static ULONGLONG g_transcriptEventMs = 0;

static long Clamp(long v)
{
	if (v > GET_OFFSET_MAX_SECONDS) return GET_OFFSET_MAX_SECONDS;
	if (v < -GET_OFFSET_MAX_SECONDS) return -GET_OFFSET_MAX_SECONDS;
	return v;
}

// Reads a run of digits (at most 7); false if there are none.
static bool ReadNumber(const char *&p, long &out)
{
	if (!isdigit((unsigned char)*p))
		return false;
	long v = 0;
	int n = 0;
	while (isdigit((unsigned char)*p))
	{
		if (++n > 6) return false;
		v = v * 10 + (*p - '0');
		++p;
	}
	out = v;
	return true;
}

bool GetOffsetParse(const char *text, long &outSeconds)
{
	if (!text)
		return false;
	const char *p = text;
	while (isspace((unsigned char)*p)) ++p;
	bool negative = false;
	if (*p == '+' || *p == '-')
	{
		negative = (*p == '-');
		++p;
	}
	long first = 0;
	if (!ReadNumber(p, first))
		return false;
	long total = first;
	if (*p == ':')
	{
		++p;
		long sec = 0;
		if (first > GET_OFFSET_MAX_SECONDS / 60 || !ReadNumber(p, sec) || sec > 59)
			return false;
		total = first * 60 + sec;
	}
	while (isspace((unsigned char)*p)) ++p;
	if (*p != '\0' || total > GET_OFFSET_MAX_SECONDS)
		return false;
	outSeconds = negative ? -total : total;
	return true;
}

void GetOffsetFormat(long seconds, char *buffer, int bufferSize)
{
	if (bufferSize <= 0)
		return;
	long m = seconds < 0 ? -seconds : seconds;
	_snprintf(buffer, bufferSize, "%c%ld:%02ld", seconds < 0 ? '-' : '+', m / 60, m % 60);
	buffer[bufferSize - 1] = '\0';
}

double GetOffsetApplyToGET(double rawSeconds)
{
	return rawSeconds + (double)g_applied;
}

long GetOffsetApplied() { return g_applied; }
bool GetOffsetHasCandidate() { return g_hasCandidate; }
long GetOffsetCandidate() { return g_candidate; }
bool GetOffsetCandidateApplied() { return g_hasCandidate && g_candidateApplied; }

bool GetOffsetCandidateIsCorrection() { return g_hasCandidate && g_candidateIsCorrection; }
long GetOffsetResulting()
{
	if (g_hasCandidate && !g_candidateApplied)
		return g_candidateIsCorrection ? Clamp(g_applied + g_candidate) : g_candidate;
	return g_applied;
}

bool GetOffsetHasSimEvent() { return g_hasSimEvent; }
bool GetOffsetHasTranscriptEvent() { return g_hasTranscriptEvent; }

static void RecalculateEventCandidate()
{
	if (!g_hasSimEvent || !g_hasTranscriptEvent)
		return;

	bool positive = g_transcriptEventMs >= g_simEventMs;
	ULONGLONG difference = positive
		? g_transcriptEventMs - g_simEventMs
		: g_simEventMs - g_transcriptEventMs;
	long seconds = difference >= (ULONGLONG)GET_OFFSET_MAX_SECONDS * 1000 + 500
		? GET_OFFSET_MAX_SECONDS
		: (long)((difference + 500) / 1000);
	g_candidate = positive ? seconds : -seconds;
	g_hasCandidate = true;
	g_candidateApplied = false;
	g_candidateIsCorrection = true;
}

void GetOffsetMarkSimEvent(ULONGLONG timestampMs)
{
	g_simEventMs = timestampMs;
	g_hasSimEvent = true;
	RecalculateEventCandidate();
}

void GetOffsetMarkTranscriptEvent(ULONGLONG timestampMs)
{
	g_transcriptEventMs = timestampMs;
	g_hasTranscriptEvent = true;
	RecalculateEventCandidate();
}

void GetOffsetSetCandidate(long seconds)
{
	g_candidate = Clamp(seconds);
	g_hasCandidate = true;
	g_candidateApplied = false;
	g_candidateIsCorrection = false;
}

bool GetOffsetFlipCandidate()
{
	if (!g_hasCandidate || g_candidateApplied)
		return false;
	g_candidate = -g_candidate;
	g_candidateApplied = false;
	return true;
}

bool GetOffsetApplyCandidate()
{
	if (!g_hasCandidate || g_candidateApplied)
		return false;
	if (g_candidateIsCorrection)
	{
		g_applied = Clamp(g_applied + g_candidate);
		g_hasSimEvent = false;
		g_hasTranscriptEvent = false;
		g_simEventMs = 0;
		g_transcriptEventMs = 0;
	}
	else
		g_applied = g_candidate;
	g_candidateApplied = true;
	return true;
}

void GetOffsetReset()
{
	g_applied = 0;
	g_hasCandidate = false;
	g_candidateApplied = false;
	g_candidateIsCorrection = false;
	g_candidate = 0;
	g_hasSimEvent = false;
	g_hasTranscriptEvent = false;
	g_simEventMs = 0;
	g_transcriptEventMs = 0;
}
