// Test harness: runs the real GroundElapsedTimeExport.cpp against a stubbed
// Orbiter API and checks the named-pipe protocol and update cadence.
#include "Orbitersdk.h"
#include "GroundElapsedTimeCommon.h"
#include "GroundElapsedTimeExport.h"
#include "GroundElapsedTimeOffset.h"
#include <stdio.h>
#include <string.h>

static VESSEL g_vessel;
static double g_met = 11724.0; // 3:15:24
static double g_accel = 10.0;
static bool g_supported = true;

VESSEL *oapiGetFocusInterface() { return &g_vessel; }
double oapiGetTimeAcceleration() { return g_accel; }
bool ComputeGroundElapsedTime(VESSEL *, double &out) { if (!g_supported) return false; out = g_met; return true; }
// Same formatting as GroundElapsedTimeCommon.cpp (that file needs NASSP headers).
void FormatGroundElapsedTimeHMS(double t, char *b, int n)
{
	long s = (long)t, m = s < 0 ? -s : s;
	if (s < 0) sprintf(b, "-%ld:%02ld:%02ld", m / 3600, (m / 60) % 60, m % 60);
	else sprintf(b, "%ld:%02ld:%02ld", m / 3600, (m / 60) % 60, m % 60);
	if (n > 0) b[n - 1] = 0;
}

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } } while (0)

static bool ReadMsg(HANDLE p, char *buf, DWORD &n)
{
	return ReadFile(p, buf, 127, &n, NULL) && (buf[n] = 0, true);
}

int main()
{
	GroundElapsedTimeExportInit();
	HANDLE p = INVALID_HANDLE_VALUE;
	for (int i = 0; i < 100 && p == INVALID_HANDLE_VALUE; i++)
	{
		p = CreateFileA("\\\\.\\pipe\\GroundElapsedTimeTest", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (p == INVALID_HANDLE_VALUE) Sleep(50);
	}
	CHECK(p != INVALID_HANDLE_VALUE, "cannot connect to pipe");
	if (p == INVALID_HANDLE_VALUE) return 1;
	DWORD mode = PIPE_READMODE_MESSAGE;
	SetNamedPipeHandleState(p, &mode, NULL, NULL);

	GroundElapsedTimeExportStep(0);
	char buf[128]; DWORD n;
	ReadMsg(p, buf, n); // initial snapshot (may be the default or first step)
	printf("first message: %s", buf);

	// Cadence: steady value, Step called every ~5 ms for 2 s -> expect ~20 messages (10 Hz).
	HANDLE th = CreateThread(NULL, 0, [](LPVOID) -> DWORD {
		for (int i = 0; i < 400; i++) { GroundElapsedTimeExportStep(i * 0.005); Sleep(5); }
		return 0; }, NULL, 0, NULL);
	int count = 0; ULONGLONG start = GetTickCount64();
	while (GetTickCount64() - start < 2000)
	{
		DWORD avail = 0;
		if (PeekNamedPipe(p, NULL, 0, NULL, &avail, NULL) && avail)
		{
			ReadMsg(p, buf, n); count++;
			CHECK(!strcmp(buf, "3:15:24\n10\n"), "bad steady message '%s'", buf);
		}
		else Sleep(2);
	}
	WaitForSingleObject(th, 5000);
	printf("steady-state messages in 2 s: %d (expect ~20)\n", count);
	CHECK(count >= 15 && count <= 25, "cadence %d not ~10 Hz", count);

	// Change publishes promptly with exact payload format.
	while (true) { DWORD a = 0; if (!PeekNamedPipe(p, NULL, 0, NULL, &a, NULL) || !a) break; ReadMsg(p, buf, n); }
	g_met = -5.0; g_accel = 0.5;
	GroundElapsedTimeExportStep(1);
	ReadMsg(p, buf, n);
	CHECK(!strcmp(buf, "-0:00:05\n0.5\n"), "bad changed message '%s'", buf);
	g_supported = false; g_accel = 1.0;
	GroundElapsedTimeExportStep(2);
	ReadMsg(p, buf, n);
	CHECK(!strcmp(buf, "0:00:00\n1\n"), "bad unsupported message '%s'", buf);

	// Offsets: parsing/formatting.
	long s;
	CHECK(GetOffsetParse("+2:30", s) && s == 150, "parse +2:30");
	CHECK(GetOffsetParse("-2:30", s) && s == -150, "parse -2:30");
	CHECK(GetOffsetParse(" 45 ", s) && s == 45, "parse 45");
	CHECK(GetOffsetParse("-0:05", s) && s == -5, "parse -0:05");
	CHECK(!GetOffsetParse("2:75", s) && !GetOffsetParse("abc", s) && !GetOffsetParse("", s) &&
		!GetOffsetParse("1:2:3", s) && !GetOffsetParse("+", s) && !GetOffsetParse("2:", s), "reject invalid");
	char f[32];
	GetOffsetFormat(150, f, sizeof(f)); CHECK(!strcmp(f, "+2:30"), "format %s", f);
	GetOffsetFormat(-5, f, sizeof(f)); CHECK(!strcmp(f, "-0:05"), "format %s", f);
	GetOffsetFormat(0, f, sizeof(f)); CHECK(!strcmp(f, "+0:00"), "format %s", f);

	// Independent event marks derive transcript timestamp - sim timestamp, a
	// correction ADDED to the applied offset by Apply.
	GetOffsetReset();
	GetOffsetMarkSimEvent(100000);
	CHECK(GetOffsetHasSimEvent() && !GetOffsetHasTranscriptEvent() && !GetOffsetHasCandidate(),
		"first mark waits for other event");
	GetOffsetMarkTranscriptEvent(250400);
	CHECK(GetOffsetHasCandidate() && GetOffsetCandidate() == 150 && GetOffsetCandidateIsCorrection(),
		"transcript after sim: +150 correction, got %ld", GetOffsetCandidate());
	CHECK(GetOffsetApplied() == 0 && !GetOffsetCandidateApplied(), "not applied before APL");
	CHECK(GetOffsetApplyToGET(100.0) == 100.0, "no offset is identity");
	CHECK(GetOffsetResulting() == 150, "resulting preview 0+150");
	CHECK(GetOffsetApplyCandidate() && GetOffsetCandidateApplied() && GetOffsetApplied() == 150,
		"apply marked correction");
	CHECK(!GetOffsetHasSimEvent() && !GetOffsetHasTranscriptEvent(), "marks cleared after applying correction");
	CHECK(!GetOffsetApplyCandidate() && GetOffsetApplied() == 150, "second Apply must not add twice");

	// Simulator GET 15 s ahead of transcript: transcript heard 15 s after sim event.
	GetOffsetReset();
	GetOffsetMarkSimEvent(1000);
	GetOffsetMarkTranscriptEvent(16000);
	CHECK(GetOffsetCandidate() == 15 && GetOffsetApplyCandidate() && GetOffsetApplied() == 15 &&
		GetOffsetApplyToGET(100.0) == 115.0, "+15 correction: output = raw + 15");
	GetOffsetReset();
	GetOffsetMarkSimEvent(100000);
	GetOffsetMarkTranscriptEvent(250400);
	GetOffsetApplyCandidate();

	// Nonzero current offset (+150): a positive correction adds, a negative one subtracts.
	GetOffsetMarkSimEvent(300000);
	GetOffsetMarkTranscriptEvent(330000);
	CHECK(GetOffsetCandidate() == 30 && !GetOffsetCandidateApplied() && GetOffsetApplied() == 150,
		"new correction +30 leaves applied unchanged (%ld)", GetOffsetCandidate());
	CHECK(GetOffsetResulting() == 180 && GetOffsetApplyToGET(1000.0) == 1150.0, "preview 150+30, no commit yet");
	CHECK(GetOffsetApplyCandidate() && GetOffsetApplied() == 180, "apply adds: 150+30=180");
	GetOffsetMarkSimEvent(500000);
	GetOffsetMarkTranscriptEvent(460000);
	CHECK(GetOffsetCandidate() == -40 && GetOffsetResulting() == 140, "negative correction preview 180-40");
	CHECK(GetOffsetApplyCandidate() && GetOffsetApplied() == 140, "apply adds negative: 140");
	// Re-marking before Apply replaces that timestamp and recomputes.
	GetOffsetMarkSimEvent(0);
	GetOffsetMarkTranscriptEvent(10000);
	GetOffsetMarkSimEvent(4000);
	CHECK(GetOffsetCandidate() == 6 && GetOffsetApplied() == 140, "recapture recomputes (%ld)", GetOffsetCandidate());
	// Manual SET stays absolute (replaces).
	GetOffsetSetCandidate(75);
	CHECK(!GetOffsetCandidateIsCorrection() && GetOffsetResulting() == 75, "manual is absolute preview");
	CHECK(GetOffsetApplyCandidate() && GetOffsetApplied() == 75, "manual apply replaces offset");
	// Explicit CLR resets to zero.
	GetOffsetReset();
	CHECK(GetOffsetApplied() == 0 && !GetOffsetHasCandidate(), "CLR resets applied offset to zero");
	// Pipe output unchanged until Apply, then raw GET + offset.
	while (true) { DWORD a = 0; if (!PeekNamedPipe(p, NULL, 0, NULL, &a, NULL) || !a) break; ReadMsg(p, buf, n); }
	GetOffsetReset();
	CHECK(!GetOffsetHasSimEvent() && !GetOffsetHasTranscriptEvent() && !GetOffsetHasCandidate() &&
		GetOffsetApplied() == 0, "clear resets event marks, candidate and applied offset");
	g_supported = true; g_met = 11724.0; g_accel = 10.0;
	GroundElapsedTimeExportStep(3);
	ReadMsg(p, buf, n);
	CHECK(!strcmp(buf, "3:15:24\n10\n"), "candidate must not change output '%s'", buf);
	GetOffsetSetCandidate(150);
	CHECK(GetOffsetApplyCandidate() && GetOffsetApplied() == 150 && GetOffsetCandidateApplied(), "apply");
	GroundElapsedTimeExportStep(4);
	ReadMsg(p, buf, n);
	CHECK(!strcmp(buf, "3:17:54\n10\n"), "positive offset adds '%s'", buf);
	GetOffsetSetCandidate(-150); GetOffsetApplyCandidate();
	GroundElapsedTimeExportStep(5);
	ReadMsg(p, buf, n);
	CHECK(!strcmp(buf, "3:12:54\n10\n"), "negative offset subtracts '%s'", buf);
	g_supported = false;
	GroundElapsedTimeExportStep(6);
	ReadMsg(p, buf, n);
	CHECK(!strcmp(buf, "0:00:00\n10\n"), "unsupported ignores offset '%s'", buf);
	GetOffsetReset();
	g_supported = true;
	GroundElapsedTimeExportStep(7);
	ReadMsg(p, buf, n);
	CHECK(!strcmp(buf, "3:15:24\n10\n"), "reset restores raw '%s'", buf);

	CloseHandle(p);
	GroundElapsedTimeExportExit();
	printf(g_fail ? "RESULT: FAILED\n" : "RESULT: PASSED\n");
	return g_fail ? 1 : 0;
}
