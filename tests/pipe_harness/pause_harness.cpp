#include "GroundElapsedTimePause.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int failures;
static HANDLE connectPending, writePending;
void PauseTestConnectPending() { SetEvent(connectPending); }
void PauseTestWritePending() { SetEvent(writePending); }
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static HANDLE Connect()
{
	for (int i = 0; i < 100; ++i)
	{
		HANDLE pipe = CreateFileA("\\\\.\\pipe\\GroundElapsedTimePauseTest",
			GENERIC_READ | FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (pipe != INVALID_HANDLE_VALUE)
		{
			DWORD mode = PIPE_READMODE_MESSAGE;
			SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
			return pipe;
		}
		Sleep(20);
	}
	return INVALID_HANDLE_VALUE;
}

static bool Read(HANDLE pipe, unsigned long long &sequence, int &paused, DWORD timeout)
{
	ULONGLONG start = GetTickCount64();
	while (GetTickCount64() - start < timeout)
	{
		DWORD available = 0, n = 0;
		if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL)) return false;
		if (available)
		{
			char text[64];
			if (!ReadFile(pipe, text, sizeof(text) - 1, &n, NULL)) return false;
			text[n] = 0;
			return sscanf(text, "1 %llu %d\n", &sequence, &paused) == 2;
		}
		Sleep(1);
	}
	return false;
}

static bool ReadSequence(HANDLE pipe, unsigned long long expected, int expectedPaused)
{
	ULONGLONG start = GetTickCount64();
	while (GetTickCount64() - start < 2000)
	{
		unsigned long long sequence;
		int paused;
		if (!Read(pipe, sequence, paused, 500)) return false;
		if (sequence == expected) return paused == expectedPaused;
		if (sequence > expected) return false;
	}
	return false;
}

int main()
{
	connectPending = CreateEvent(NULL, TRUE, FALSE, NULL);
	writePending = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!connectPending || !writePending) return 1;
	GroundElapsedTimePauseInit(true);
	HANDLE pipe = Connect();
	CHECK(pipe != INVALID_HANDLE_VALUE, "connect");
	if (pipe == INVALID_HANDLE_VALUE) { GroundElapsedTimePauseExit(); return 1; }
	CHECK(ReadSequence(pipe, 0, 1), "initially paused snapshot");
	GroundElapsedTimePauseChanged(true);
	CHECK(ReadSequence(pipe, 0, 1), "unchanged state keeps sequence");
	GroundElapsedTimePauseChanged(false);
	GroundElapsedTimePauseChanged(true);
	GroundElapsedTimePauseChanged(false);
	CHECK(ReadSequence(pipe, 1, 0), "resume without simulation step");
	CHECK(ReadSequence(pipe, 2, 1), "pause without simulation step");
	CHECK(ReadSequence(pipe, 3, 0), "rapid transitions remain ordered");
	CloseHandle(pipe);
	GroundElapsedTimePauseChanged(true);
	GroundElapsedTimePauseChanged(false);
	pipe = Connect();
	CHECK(pipe != INVALID_HANDLE_VALUE, "reconnect");
	if (pipe != INVALID_HANDLE_VALUE)
	{
		unsigned long long sequence = 0;
		int paused = -1;
		CHECK(Read(pipe, sequence, paused, 2000) && sequence == 5 && paused == 0,
			"reconnect starts with latest snapshot");
		ULONGLONG maxCallMs = 0;
		for (int i = 0; i < 200000; ++i)
		{
			ULONGLONG before = GetTickCount64();
			GroundElapsedTimePauseChanged((i & 1) == 0);
			ULONGLONG elapsed = GetTickCount64() - before;
			if (elapsed > maxCallMs) maxCallMs = elapsed;
		}
		printf("200000 pause transitions: max callback %llu ms\n", maxCallMs);
		CHECK(maxCallMs < 100, "pause callback blocked on stalled reader");
		bool disconnected = false;
		ULONGLONG start = GetTickCount64();
		while (GetTickCount64() - start < 5000)
		{
			DWORD available;
			if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL))
			{
				disconnected = true;
				break;
			}
			if (available)
			{
				char text[64]; DWORD n;
				if (!ReadFile(pipe, text, sizeof(text), &n, NULL)) { disconnected = true; break; }
			}
			else Sleep(1);
		}
		CHECK(disconnected, "overflow disconnects rather than silently dropping transitions");
		CloseHandle(pipe);
		pipe = Connect();
		CHECK(pipe != INVALID_HANDLE_VALUE, "reconnect after overflow");
		if (pipe != INVALID_HANDLE_VALUE)
		{
			CHECK(Read(pipe, sequence, paused, 2000) && sequence == 200005 && paused == 0,
				"overflow reconnect starts with latest state");
			GroundElapsedTimePauseExit();
			DWORD available = 0;
			CHECK(!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL), "shutdown disconnects client");
			CloseHandle(pipe);
		}
		else GroundElapsedTimePauseExit();
		GroundElapsedTimePauseInit(false);
		pipe = Connect();
		CHECK(pipe != INVALID_HANDLE_VALUE, "connect after restart");
		if (pipe != INVALID_HANDLE_VALUE)
		{
			CHECK(ReadSequence(pipe, 0, 0), "initially running snapshot");
			GroundElapsedTimePauseExit();
			CloseHandle(pipe);
		}
		else GroundElapsedTimePauseExit();
	}
	else GroundElapsedTimePauseExit();

	ResetEvent(connectPending);
	GroundElapsedTimePauseInit(false);
	CHECK(WaitForSingleObject(connectPending, 2000) == WAIT_OBJECT_0,
		"connect operation actually pending");
	ULONGLONG before = GetTickCount64();
	GroundElapsedTimePauseExit();
	CHECK(GetTickCount64() - before < 2000, "shutdown cancels pending connect");

	ResetEvent(writePending);
	GroundElapsedTimePauseInit(false);
	pipe = Connect();
	CHECK(pipe != INVALID_HANDLE_VALUE, "connect stalled client");
	if (pipe != INVALID_HANDLE_VALUE)
	{
		for (int i = 0; i < 5000 && WaitForSingleObject(writePending, 0) != WAIT_OBJECT_0; ++i)
		{
			GroundElapsedTimePauseChanged((i & 1) == 0);
			Sleep(1);
		}
		CHECK(WaitForSingleObject(writePending, 0) == WAIT_OBJECT_0,
			"write operation actually pending");
		before = GetTickCount64();
		GroundElapsedTimePauseExit();
		CHECK(GetTickCount64() - before < 2000, "shutdown cancels stalled write");
		CloseHandle(pipe);
	}
	else GroundElapsedTimePauseExit();
	CloseHandle(connectPending);
	CloseHandle(writePending);
	printf(failures ? "RESULT: FAILED\n" : "RESULT: PASSED\n");
	return failures ? 1 : 0;
}
