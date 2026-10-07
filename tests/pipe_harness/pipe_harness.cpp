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

// Timed read: returns false (buf = "") if no message arrives within timeoutMs.
static bool RecvTimed(HANDLE p, char *buf, DWORD timeoutMs)
{
	ULONGLONG start = GetTickCount64();
	buf[0] = 0;
	while (GetTickCount64() - start <= timeoutMs)
	{
		DWORD avail = 0, n = 0;
		if (!PeekNamedPipe(p, NULL, 0, NULL, &avail, NULL)) return false;
		if (avail)
			return ReadFile(p, buf, 127, &n, NULL) && (buf[n] = 0, true);
		Sleep(1);
	}
	return false;
}
static bool ReadMsg(HANDLE p, char *buf, DWORD &n)
{
	bool ok = RecvTimed(p, buf, 3000);
	n = (DWORD)strlen(buf);
	return ok;
}
static int Drain(HANDLE p)
{
	char b[128]; int c = 0;
	Sleep(30);
	while (RecvTimed(p, b, 0)) c++;
	return c;
}
static void Expect(HANDLE p, const char *want, const char *what)
{
	char b[128];
	bool ok = RecvTimed(p, b, 1000);
	CHECK(ok && !strcmp(b, want), "%s: expected '%.20s' got '%.20s'", what, want, b);
}
static void ExpectNone(HANDLE p, const char *what)
{
	char b[128];
	CHECK(!RecvTimed(p, b, 150), "%s: unexpected message '%.20s'", what, b);
}
static void Sample(double met, double accel = 10.0, bool supported = true)
{
	g_met = met; g_accel = accel; g_supported = supported;
	GroundElapsedTimeExportStep(0);
}

int main()
{
	GroundElapsedTimeExportInit();
	HANDLE p = INVALID_HANDLE_VALUE;
	for (int i = 0; i < 100 && p == INVALID_HANDLE_VALUE; i++)
	{
		p = CreateFileA("\\\\.\\pipe\\GroundElapsedTimeTest", GENERIC_READ | FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (p == INVALID_HANDLE_VALUE) Sleep(50);
	}
	CHECK(p != INVALID_HANDLE_VALUE, "cannot connect to pipe");
	if (p == INVALID_HANDLE_VALUE) return 1;
	DWORD mode = PIPE_READMODE_MESSAGE;
	SetNamedPipeHandleState(p, &mode, NULL, NULL);
	char buf[128]; DWORD n;

	// New client gets the latest value immediately (default before any step).
	Expect(p, "0:00:00\n1\n", "initial snapshot");
	Sample(11724.0);
	Expect(p, "3:15:24\n10\n", "first sample publishes immediately");

	// Event-driven cadence: steady value publishes nothing but the heartbeat.
	// Step every 5 ms for 2.5 s -> heartbeats at ~1 s and ~2 s, not 10 Hz.
	{
		int count = 0; ULONGLONG start = GetTickCount64();
		while (GetTickCount64() - start < 2500)
		{
			GroundElapsedTimeExportStep(0);
			if (RecvTimed(p, buf, 0)) { count++; CHECK(!strcmp(buf, "3:15:24\n10\n"), "bad heartbeat '%s'", buf); }
			Sleep(5);
		}
		printf("steady-state messages in 2.5 s: %d (expect 2 heartbeats)\n", count);
		CHECK(count >= 1 && count <= 3, "heartbeat count %d not ~2", count);
	}

	// Sub-second progress inside one displayed second: nothing; each boundary: exactly one.
	Drain(p);
	Sample(11724.0); Sample(11724.5); Sample(11724.99);
	ExpectNone(p, "no boundary crossed");
	Sample(11725.01); Expect(p, "3:15:25\n10\n", "one boundary");
	ExpectNone(p, "no duplicate after boundary");
	for (int i = 1; i <= 15; i++) Sample(11725.01 + i * 0.1); // 1x-ish: crosses 26 only
	Expect(p, "3:15:26\n10\n", "slow progress crosses 1 second");
	ExpectNone(p, "slow progress extra");

	// 60x frame crossing several displayed seconds: every one, in order.
	Drain(p);
	Sample(200.2); Drain(p);
	Sample(204.7);
	Expect(p, "0:03:21\n10\n", "cross 201"); Expect(p, "0:03:22\n10\n", "cross 202");
	Expect(p, "0:03:23\n10\n", "cross 203"); Expect(p, "0:03:24\n10\n", "cross 204");
	ExpectNone(p, "after multi-second step");

	// Backward step: each second visited in descending order.
	Sample(201.3);
	Expect(p, "0:03:23\n10\n", "back 203"); Expect(p, "0:03:22\n10\n", "back 202");
	Expect(p, "0:03:21\n10\n", "back 201"); ExpectNone(p, "after backward step");
	Sample(201.9); ExpectNone(p, "forward within same second");

	// Signed GET: truncation toward zero, crossing through zero in both directions.
	Sample(-2.5); // jump (>120 s away): single current value
	Expect(p, "-0:00:02\n10\n", "jump to negative"); ExpectNone(p, "jump single");
	Sample(1.2);
	Expect(p, "-0:00:01\n10\n", "up -1"); Expect(p, "0:00:00\n10\n", "up 0"); Expect(p, "0:00:01\n10\n", "up 1");
	ExpectNone(p, "after signed forward");
	Sample(-1.5);
	Expect(p, "0:00:00\n10\n", "down 0"); Expect(p, "-0:00:01\n10\n", "down -1");
	ExpectNone(p, "after signed backward");
	Sample(-0.5); Expect(p, "0:00:00\n10\n", "-1 -> 0 boundary");
	Sample(0.5); ExpectNone(p, "-0.5 and 0.5 both display 0:00:00");

	// Large jump (beyond catch-up limit) publishes only the current value.
	Sample(5000.0); Expect(p, "1:23:20\n10\n", "large forward jump"); ExpectNone(p, "large jump single");
	Sample(5000.0 + 120.0); // exactly the limit: expanded
	for (int i = 1; i <= 120; i++) { char w[32]; sprintf(w, "1:%02d:%02d\n10\n", (5000 + i) / 60 % 60, (5000 + i) % 60); Expect(p, w, "limit expansion"); }
	ExpectNone(p, "after limit expansion");
	Sample(5000.0 + 120.0 + 121.0); Expect(p, "1:27:21\n10\n", "beyond limit is a jump"); ExpectNone(p, "beyond limit single");

	// Acceleration change alone publishes the current value immediately; same again does not.
	Sample(5241.0, 0.5); Expect(p, "1:27:21\n0.5\n", "accel change"); ExpectNone(p, "accel steady");
	// Crossings carry the current acceleration.
	Sample(5243.5, 0.5);
	Expect(p, "1:27:22\n0.5\n", "cross w/ accel"); Expect(p, "1:27:23\n0.5\n", "cross w/ accel 2");
	// Unsupported <-> supported is a discontinuity, not a time step.
	Sample(5243.5, 1.0, false); Expect(p, "0:00:00\n1\n", "unsupported");
	Sample(9999.0, 1.0, true); Expect(p, "2:46:39\n1\n", "supported again"); ExpectNone(p, "supported single");

	// Applying/clearing an offset re-baselines: current value once, no second-by-second replay.
	Sample(11724.0); Drain(p);
	GetOffsetSetCandidate(150); GetOffsetApplyCandidate();
	Sample(11724.0); Expect(p, "3:17:54\n10\n", "offset applied publishes immediately"); ExpectNone(p, "offset single");
	GetOffsetSetCandidate(-150); GetOffsetApplyCandidate();
	Sample(11724.0); Expect(p, "3:12:54\n10\n", "negative offset"); ExpectNone(p, "neg offset single");
	GetOffsetReset();
	Sample(11724.0); Expect(p, "3:15:24\n10\n", "offset cleared"); ExpectNone(p, "clear single");
	// Offset stays applied across crossings.
	GetOffsetSetCandidate(-3); GetOffsetApplyCandidate(); Sample(11724.0); Drain(p);
	Sample(11726.2); Expect(p, "3:15:22\n10\n", "offset crossing 1"); Expect(p, "3:15:23\n10\n", "offset crossing 2");
	GetOffsetReset(); Sample(11724.0); Drain(p);

	// Bounded queue / no blocking: reader stalled while the sim steps 2000 s.
	{
		unsigned long drop0 = GroundElapsedTimeExportDroppedCount();
		DWORD maxMs = 0;
		for (int i = 1; i <= 2000; i++)
		{
			ULONGLONG t0 = GetTickCount64();
			Sample(11724.0 + i + 0.2);
			DWORD d = (DWORD)(GetTickCount64() - t0);
			if (d > maxMs) maxMs = d;
		}
		printf("2000 steps with stalled reader: max step %lu ms, dropped %lu\n", maxMs, GroundElapsedTimeExportDroppedCount() - drop0);
		CHECK(maxMs < 50, "Step blocked %lu ms", (unsigned long)maxMs);
		CHECK(GroundElapsedTimeExportDroppedCount() > drop0, "expected drop-oldest overflow");
		Sleep(100);
		long prev = -1; int got = 0; char last[128] = "";
		while (RecvTimed(p, buf, 200))
		{
			long h, m, s; sscanf(buf, "%ld:%ld:%ld", &h, &m, &s);
			long v = h * 3600 + m * 60 + s;
			CHECK(v > prev, "out of order after overflow: %ld then %ld", prev, v);
			prev = v; got++; strcpy(last, buf);
		}
		printf("received %d messages after overflow (last %.8s)\n", got, last);
		CHECK(got > 0 && got < 2000, "bounded delivery %d", got);
		CHECK(!strcmp(last, "3:48:44\n10\n"), "newest value must survive overflow, got '%.12s'", last);
	}
	Sample(11724.0, 10.0, true);
	Drain(p);
	g_met = 11724.0; g_accel = 10.0; g_supported = true;

	// Change publishes promptly with exact payload format.
	Sample(-5.0, 0.5);
	Expect(p, "-0:00:05\n0.5\n", "changed message");
	Sample(-5.0, 1.0, false);
	Expect(p, "0:00:00\n1\n", "unsupported message");
	Drain(p);
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
