/***************************************************************************
  GroundElapsedTime named-pipe exporter (implementation).

  Threading model
  - Orbiter's simulation thread only calls GroundElapsedTimeExportStep().
    It formats a tiny ASCII message and, when it changed (or every 100 ms
    of wall-clock time), copies it into a shared slot under a critical
    section held only for a memcpy, then signals an event. It never performs
    pipe I/O and never waits for a client.
  - A worker thread owns the pipe. It uses overlapped (asynchronous) I/O
    for connect and write, and every wait also watches a stop event, so
    shutdown is prompt. A stalled client is dropped after a write timeout.
  ***************************************************************************/

#pragma include_alias( <fstream.h>, <fstream> )
#include "Orbitersdk.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "GroundElapsedTimeCommon.h"
#include "GroundElapsedTimeExport.h"

static const char *PIPE_NAME = "\\\\.\\pipe\\GroundElapsedTime";
static const DWORD PIPE_BUFFER_BYTES = 4096;
static const DWORD WRITE_TIMEOUT_MS = 1000;
static const DWORD RETRY_CREATE_MS = 1000;
static const ULONGLONG REPUBLISH_INTERVAL_MS = 100;
static const size_t MESSAGE_MAX = 128;

static CRITICAL_SECTION g_lock;
static bool g_lockReady = false;
static char g_message[MESSAGE_MAX] = "0:00:00\n1\n";
static DWORD g_messageLen = 10;

static HANDLE g_thread = NULL;
static HANDLE g_stopEvent = NULL;
static HANDLE g_updateEvent = NULL;

// Main-thread-only state.
static char g_lastPublished[MESSAGE_MAX] = "";
static ULONGLONG g_lastPublishTick = 0;
static bool g_everPublished = false;

static void FormatAcceleration(double accel, char *buffer, int size)
{
	// Fixed notation with up to 3 decimals, trailing zeros trimmed, '.' as
	// the decimal separator regardless of locale.
	_snprintf(buffer, size, "%.3f", accel);
	buffer[size - 1] = '\0';
	for (char *p = buffer; *p; ++p)
		if (*p == ',')
			*p = '.';
	char *dot = strchr(buffer, '.');
	if (dot)
	{
		char *end = buffer + strlen(buffer) - 1;
		while (end > dot && *end == '0')
			*end-- = '\0';
		if (end == dot)
			*end = '\0';
	}
}

static void SnapshotMessage(char *out, DWORD &len)
{
	EnterCriticalSection(&g_lock);
	len = g_messageLen;
	memcpy(out, g_message, len);
	LeaveCriticalSection(&g_lock);
}

// Writes one complete message. Returns false if the client is gone/stalled.
static bool WriteMessage(HANDLE pipe, HANDLE ioEvent, const char *msg, DWORD len)
{
	OVERLAPPED ov = {};
	ov.hEvent = ioEvent;
	ResetEvent(ioEvent);

	DWORD written = 0;
	if (!WriteFile(pipe, msg, len, &written, &ov))
	{
		if (GetLastError() != ERROR_IO_PENDING)
			return false;
		HANDLE waits[2] = { ioEvent, g_stopEvent };
		DWORD w = WaitForMultipleObjects(2, waits, FALSE, WRITE_TIMEOUT_MS);
		if (w != WAIT_OBJECT_0)
		{
			CancelIoEx(pipe, &ov);
			GetOverlappedResult(pipe, &ov, &written, TRUE);
			return false;
		}
		if (!GetOverlappedResult(pipe, &ov, &written, FALSE))
			return false;
	}
	return written == len;
}

static DWORD WINAPI PipeThread(LPVOID)
{
	HANDLE connectEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	HANDLE ioEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	HANDLE pipe = INVALID_HANDLE_VALUE;
	bool first = true;

	while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0)
	{
		if (pipe == INVALID_HANDLE_VALUE)
		{
			// Only the first instance claims the name exclusively, so a
			// second Orbiter/plugin copy cannot hijack or duplicate it.
			pipe = CreateNamedPipeA(PIPE_NAME,
				PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				1, PIPE_BUFFER_BYTES, 0, 0, NULL);
			if (pipe == INVALID_HANDLE_VALUE)
			{
				if (WaitForSingleObject(g_stopEvent, RETRY_CREATE_MS) == WAIT_OBJECT_0)
					break;
				continue;
			}
			first = false;
		}

		OVERLAPPED ov = {};
		ov.hEvent = connectEvent;
		ResetEvent(connectEvent);
		bool connected = false;
		if (ConnectNamedPipe(pipe, &ov))
			connected = true;
		else
		{
			DWORD err = GetLastError();
			if (err == ERROR_PIPE_CONNECTED)
				connected = true;
			else if (err == ERROR_IO_PENDING)
			{
				HANDLE waits[2] = { connectEvent, g_stopEvent };
				if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
				{
					CancelIoEx(pipe, &ov);
					break;
				}
				connected = true;
			}
		}

		if (!connected)
		{
			CloseHandle(pipe);
			pipe = INVALID_HANDLE_VALUE;
			if (WaitForSingleObject(g_stopEvent, RETRY_CREATE_MS) == WAIT_OBJECT_0)
				break;
			continue;
		}

		// A new client immediately receives the latest value.
		char msg[MESSAGE_MAX];
		DWORD len;
		SnapshotMessage(msg, len);
		bool ok = WriteMessage(pipe, ioEvent, msg, len);

		while (ok)
		{
			HANDLE waits[2] = { g_updateEvent, g_stopEvent };
			if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
				break;
			SnapshotMessage(msg, len);
			ok = WriteMessage(pipe, ioEvent, msg, len);
		}

		// Client left (or we are stopping): reuse the same pipe instance.
		DisconnectNamedPipe(pipe);
	}

	if (pipe != INVALID_HANDLE_VALUE)
		CloseHandle(pipe);
	CloseHandle(connectEvent);
	CloseHandle(ioEvent);
	return 0;
}

void GroundElapsedTimeExportInit()
{
	if (g_thread)
		return;
	if (!g_lockReady)
	{
		InitializeCriticalSection(&g_lock);
		g_lockReady = true;
	}
	g_lastPublished[0] = '\0';
	g_everPublished = false;
	g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	g_updateEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_thread = CreateThread(NULL, 0, PipeThread, NULL, 0, NULL);
}

void GroundElapsedTimeExportExit()
{
	if (g_thread)
	{
		SetEvent(g_stopEvent);
		// Every wait in the worker observes g_stopEvent; the timeout is only
		// a safety net so Orbiter can never hang on exit.
		if (WaitForSingleObject(g_thread, 3000) == WAIT_TIMEOUT)
			TerminateThread(g_thread, 0);
		CloseHandle(g_thread);
		g_thread = NULL;
	}
	if (g_stopEvent) { CloseHandle(g_stopEvent); g_stopEvent = NULL; }
	if (g_updateEvent) { CloseHandle(g_updateEvent); g_updateEvent = NULL; }
	if (g_lockReady)
	{
		DeleteCriticalSection(&g_lock);
		g_lockReady = false;
	}
}

void GroundElapsedTimeExportStep(double simt)
{
	if (!g_thread)
		return;

	VESSEL *focusVessel = oapiGetFocusInterface();
	double mt = 0.0;
	if (!focusVessel || !ComputeGroundElapsedTime(focusVessel, mt))
		mt = 0.0;

	char hms[64];
	char accel[64];
	FormatGroundElapsedTimeHMS(mt, hms, sizeof(hms));
	FormatAcceleration(oapiGetTimeAcceleration(), accel, sizeof(accel));

	char msg[MESSAGE_MAX];
	int n = _snprintf(msg, sizeof(msg) - 1, "%s\n%s\n", hms, accel);
	if (n < 0)
		return;
	msg[n] = '\0';

	// Publish on any change, plus a wall-clock heartbeat so a client that
	// connects late or a paused simulation still sees a fresh message.
	ULONGLONG now = GetTickCount64();
	if (g_everPublished && strcmp(msg, g_lastPublished) == 0 &&
		(now - g_lastPublishTick) < REPUBLISH_INTERVAL_MS)
		return;

	strcpy(g_lastPublished, msg);
	g_lastPublishTick = now;
	g_everPublished = true;

	EnterCriticalSection(&g_lock);
	memcpy(g_message, msg, n + 1);
	g_messageLen = (DWORD)n;
	LeaveCriticalSection(&g_lock);
	SetEvent(g_updateEvent);
}
