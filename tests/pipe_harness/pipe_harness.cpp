// Test harness: runs the real GroundElapsedTimeExport.cpp against a stubbed
// Orbiter API and checks the named-pipe protocol and update cadence.
#include "Orbitersdk.h"
#include "GroundElapsedTimeCommon.h"
#include "GroundElapsedTimeExport.h"
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
		p = CreateFileA("\\\\.\\pipe\\GroundElapsedTime", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
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

	CloseHandle(p);
	GroundElapsedTimeExportExit();
	printf(g_fail ? "RESULT: FAILED\n" : "RESULT: PASSED\n");
	return g_fail ? 1 : 0;
}
