/***************************************************************************
  GroundElapsedTime named-pipe exporter (implementation).

  Threading model
  - Orbiter's simulation thread only calls GroundElapsedTimeExportStep().
    It tracks the displayed (whole-second, truncated toward zero) GET and
    enqueues one tiny ASCII message for EVERY displayed second crossed since
    the previous sample, in order, in either direction. Large jumps, offset
    changes, focus changes and unsupported<->supported changes are treated as
    discontinuities and publish only the current value. Besides changes, a
    wall-clock heartbeat republishes the current value. Messages go into a
    bounded queue under a critical section held only for a memcpy; when the
    queue is full the oldest entry is dropped. The sim thread never performs
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
#include "GroundElapsedTimeOffset.h"

// The test harness builds this file with its own pipe name so it cannot clash
// with a running Orbiter.
#ifdef GET_TEST_PIPE_NAME
static const char *PIPE_NAME = GET_TEST_PIPE_NAME;
#else
static const char *PIPE_NAME = "\\\\.\\pipe\\GroundElapsedTime";
#endif
static const DWORD PIPE_BUFFER_BYTES = 4096;
static const DWORD WRITE_TIMEOUT_MS = 1000;
static const DWORD RETRY_CREATE_MS = 1000;
#ifndef GET_HEARTBEAT_MS
#define GET_HEARTBEAT_MS 1000
#endif
static const ULONGLONG HEARTBEAT_INTERVAL_MS = GET_HEARTBEAT_MS;
// Largest displayed-second step (either direction) that is expanded into one
// message per second. 120 s per sim step is 7200x at 60 steps/s. Larger steps
// are discontinuities (jump, scenario change) and publish only the current value.
static const long MAX_CATCHUP_SECONDS = 120;
static const size_t MESSAGE_MAX = 128;
static const int QUEUE_CAPACITY = 256;

static CRITICAL_SECTION g_lock;
static bool g_lockReady = false;
// Latest published message (what a newly connected client receives first).
static char g_message[MESSAGE_MAX] = "0:00:00\n1\n";
static DWORD g_messageLen = 10;
// Bounded FIFO of messages waiting for the worker. Guarded by g_lock.
struct QueuedMessage { char text[MESSAGE_MAX]; DWORD len; };
static QueuedMessage g_queue[QUEUE_CAPACITY];
static int g_queueHead = 0;
static int g_queueCount = 0;
static volatile LONG g_droppedCount = 0;

static HANDLE g_thread = NULL;
static HANDLE g_stopEvent = NULL;
static HANDLE g_updateEvent = NULL;

// Main-thread-only state.
static bool g_haveBase = false;
static long g_baseSecond = 0;
static VESSEL *g_baseVessel = NULL;
static bool g_baseSupported = false;
static long g_baseOffset = 0;
static char g_baseAccel[64] = "";
static ULONGLONG g_lastPublishTick = 0;

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

// A newly connected client gets the latest value only: stale queued entries
// from before the connection are discarded in the same critical section.
static void SnapshotLatestAndClear(char *out, DWORD &len)
{
	EnterCriticalSection(&g_lock);
	len = g_messageLen;
	memcpy(out, g_message, len);
	g_queueHead = 0;
	g_queueCount = 0;
	LeaveCriticalSection(&g_lock);
}

static bool PopQueued(char *out, DWORD &len)
{
	bool got = false;
	EnterCriticalSection(&g_lock);
	if (g_queueCount > 0)
	{
		const QueuedMessage &q = g_queue[g_queueHead];
		len = q.len;
		memcpy(out, q.text, len);
		g_queueHead = (g_queueHead + 1) % QUEUE_CAPACITY;
		g_queueCount--;
		got = true;
	}
	LeaveCriticalSection(&g_lock);
	return got;
}

// Sim thread: append to the bounded queue (dropping the oldest when full) and
// update the latest-value slot. Only a memcpy under the lock; never blocks on I/O.
static void EnqueueMessage(const char *text, int n)
{
	EnterCriticalSection(&g_lock);
	memcpy(g_message, text, n + 1);
	g_messageLen = (DWORD)n;
	if (g_queueCount == QUEUE_CAPACITY)
	{
		g_queueHead = (g_queueHead + 1) % QUEUE_CAPACITY;
		g_queueCount--;
		InterlockedIncrement(&g_droppedCount);
	}
	QueuedMessage &q = g_queue[(g_queueHead + g_queueCount) % QUEUE_CAPACITY];
	memcpy(q.text, text, n + 1);
	q.len = (DWORD)n;
	g_queueCount++;
	LeaveCriticalSection(&g_lock);
	SetEvent(g_updateEvent);
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
		SnapshotLatestAndClear(msg, len);
		bool ok = WriteMessage(pipe, ioEvent, msg, len);

		while (ok)
		{
			// Drain everything queued, in order, before sleeping again.
			while (ok && PopQueued(msg, len))
			{
				if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0)
				{
					ok = false;
					break;
				}
				ok = WriteMessage(pipe, ioEvent, msg, len);
			}
			if (!ok)
				break;
			HANDLE waits[2] = { g_updateEvent, g_stopEvent };
			if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
				break;
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
	g_haveBase = false;
	g_baseVessel = NULL;
	g_baseAccel[0] = '\0';
	g_queueHead = 0;
	g_queueCount = 0;
	g_droppedCount = 0;
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
	bool supported = false;
	double mt = 0.0;
	if (focusVessel && ComputeGroundElapsedTime(focusVessel, mt))
	{
		supported = true;
		mt = GetOffsetApplyToGET(mt); // identity while no offset is applied
	}
	else
		mt = 0.0;
	if (!(mt > -2.0e9 && mt < 2.0e9)) // also rejects NaN
		mt = 0.0;

	// Displayed second: GET is shown truncated toward zero, so consecutive
	// displayed values are contiguous integers (..., -1, 0, 1, ...) in both signs.
	long second = (long)mt;
	long offset = GetOffsetApplied();

	char accel[64];
	FormatAcceleration(oapiGetTimeAcceleration(), accel, sizeof(accel));

	// Anything that is not continuous sim-time progression re-baselines
	// and publishes only the current value.
	bool discontinuity = !g_haveBase || focusVessel != g_baseVessel ||
		supported != g_baseSupported || offset != g_baseOffset;
	long delta = discontinuity ? 0 : second - g_baseSecond;
	if (delta > MAX_CATCHUP_SECONDS || delta < -MAX_CATCHUP_SECONDS)
		discontinuity = true;
	bool accelChanged = strcmp(accel, g_baseAccel) != 0;

	ULONGLONG now = GetTickCount64();
	char hms[64];
	char msg[MESSAGE_MAX];
	int n;

	if (!discontinuity && delta != 0)
	{
		// Every displayed second crossed since the last sample, in order
		// (ascending when time advanced, descending when it moved back).
		long stepDir = delta > 0 ? 1 : -1;
		for (long s = g_baseSecond + stepDir; ; s += stepDir)
		{
			FormatGroundElapsedTimeHMS((double)s, hms, sizeof(hms));
			n = _snprintf(msg, sizeof(msg) - 1, "%s\n%s\n", hms, accel);
			if (n >= 0)
			{
				msg[n] = '\0';
				EnqueueMessage(msg, n);
			}
			if (s == second)
				break;
		}
		g_lastPublishTick = now;
	}
	else if (discontinuity || accelChanged ||
		(now - g_lastPublishTick) >= HEARTBEAT_INTERVAL_MS)
	{
		// Immediate current value: first sample, discontinuity, acceleration
		// change, or the heartbeat for idle/paused periods and late clients.
		FormatGroundElapsedTimeHMS((double)second, hms, sizeof(hms));
		n = _snprintf(msg, sizeof(msg) - 1, "%s\n%s\n", hms, accel);
		if (n >= 0)
		{
			msg[n] = '\0';
			EnqueueMessage(msg, n);
			g_lastPublishTick = now;
		}
	}

	g_haveBase = true;
	g_baseSecond = second;
	g_baseVessel = focusVessel;
	g_baseSupported = supported;
	g_baseOffset = offset;
	strcpy(g_baseAccel, accel);
}

unsigned long GroundElapsedTimeExportDroppedCount()
{
	return (unsigned long)g_droppedCount;
}
