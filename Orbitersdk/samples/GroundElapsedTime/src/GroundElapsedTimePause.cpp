#include "GroundElapsedTimePause.h"
#include <windows.h>
#include <stdio.h>

#ifdef GET_TEST_PAUSE_PIPE_NAME
static const char *PIPE_NAME = GET_TEST_PAUSE_PIPE_NAME;
#else
static const char *PIPE_NAME = "\\\\.\\pipe\\GroundElapsedTimePauseV1";
#endif

static const int CAPACITY = 256;
struct State { unsigned long long sequence; bool paused; };
static CRITICAL_SECTION g_lock;
static State g_latest;
static State g_queue[CAPACITY];
static int g_head, g_count;
static bool g_overflow;
static HANDLE g_stop, g_update, g_thread;

#ifdef GET_TEST_PAUSE_IO
extern void PauseTestConnectPending();
extern void PauseTestWritePending();
#endif

static bool WriteState(HANDLE pipe, HANDLE event, const State &state)
{
	char text[64];
	int length = _snprintf(text, sizeof(text), "1 %llu %d\n",
		state.sequence, state.paused ? 1 : 0);
	OVERLAPPED ov = {};
	ov.hEvent = event;
	ResetEvent(event);
	DWORD written = 0;
	if (!WriteFile(pipe, text, length, &written, &ov))
	{
		if (GetLastError() != ERROR_IO_PENDING)
			return false;
#ifdef GET_TEST_PAUSE_IO
		PauseTestWritePending();
#endif
		HANDLE waits[] = { event, g_stop };
		if (WaitForMultipleObjects(2, waits, FALSE, 1000) != WAIT_OBJECT_0)
		{
			CancelIoEx(pipe, &ov);
			GetOverlappedResult(pipe, &ov, &written, TRUE);
			return false;
		}
		if (!GetOverlappedResult(pipe, &ov, &written, FALSE))
			return false;
	}
	return written == (DWORD)length;
}

static DWORD WINAPI PausePipeThread(LPVOID)
{
	HANDLE connectEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	HANDLE ioEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!connectEvent || !ioEvent)
	{
		OutputDebugStringA("GroundElapsedTime: pause pipe event creation failed\n");
		if (connectEvent) CloseHandle(connectEvent);
		if (ioEvent) CloseHandle(ioEvent);
		return 1;
	}
	HANDLE pipe = INVALID_HANDLE_VALUE;
	while (WaitForSingleObject(g_stop, 0) != WAIT_OBJECT_0)
	{
		if (pipe == INVALID_HANDLE_VALUE)
		{
			pipe = CreateNamedPipeA(PIPE_NAME,
				PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				1, 4096, 0, 0, NULL);
			if (pipe == INVALID_HANDLE_VALUE)
			{
				if (WaitForSingleObject(g_stop, 1000) == WAIT_OBJECT_0) break;
				continue;
			}
		}
		OVERLAPPED ov = {};
		ov.hEvent = connectEvent;
		ResetEvent(connectEvent);
		bool connected = ConnectNamedPipe(pipe, &ov) != FALSE;
		if (!connected)
		{
			DWORD error = GetLastError();
			if (error == ERROR_PIPE_CONNECTED)
				connected = true;
			else if (error == ERROR_IO_PENDING)
			{
#ifdef GET_TEST_PAUSE_IO
				PauseTestConnectPending();
#endif
				HANDLE waits[] = { connectEvent, g_stop };
				if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
				{
					CancelIoEx(pipe, &ov);
					DWORD ignored;
					GetOverlappedResult(pipe, &ov, &ignored, TRUE);
					break;
				}
				DWORD ignored;
				connected = GetOverlappedResult(pipe, &ov, &ignored, FALSE) != FALSE;
			}
		}
		if (!connected)
		{
			CloseHandle(pipe);
			pipe = INVALID_HANDLE_VALUE;
			continue;
		}

		EnterCriticalSection(&g_lock);
		State state = g_latest;
		g_head = g_count = 0;
		g_overflow = false;
		LeaveCriticalSection(&g_lock);
		bool ok = WriteState(pipe, ioEvent, state);
		while (ok && WaitForSingleObject(g_stop, 0) != WAIT_OBJECT_0)
		{
			bool overflow, have;
			EnterCriticalSection(&g_lock);
			overflow = g_overflow;
			have = g_count != 0;
			if (have && !overflow)
			{
				state = g_queue[g_head];
				g_head = (g_head + 1) % CAPACITY;
				--g_count;
			}
			LeaveCriticalSection(&g_lock);
			if (overflow) break;
			if (have)
			{
				ok = WriteState(pipe, ioEvent, state);
				continue;
			}
			HANDLE waits[] = { g_update, g_stop };
			DWORD result = WaitForMultipleObjects(2, waits, FALSE, 250);
			if (result == WAIT_TIMEOUT)
			{
				// Repeat the last sent state to detect departed readers. Queued
				// transitions always precede the next repeat.
				ok = WriteState(pipe, ioEvent, state);
			}
			else if (result != WAIT_OBJECT_0)
				break;
		}
		DisconnectNamedPipe(pipe);
	}
	if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
	CloseHandle(connectEvent);
	CloseHandle(ioEvent);
	return 0;
}

void GroundElapsedTimePauseInit(bool paused)
{
	InitializeCriticalSection(&g_lock);
	g_latest = { 0, paused };
	g_head = g_count = 0;
	g_overflow = false;
	g_stop = CreateEvent(NULL, TRUE, FALSE, NULL);
	g_update = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_stop && g_update)
		g_thread = CreateThread(NULL, 0, PausePipeThread, NULL, 0, NULL);
	if (!g_thread)
		OutputDebugStringA("GroundElapsedTime: pause pipe worker creation failed\n");
}

void GroundElapsedTimePauseChanged(bool paused)
{
	if (!g_thread) return;
	EnterCriticalSection(&g_lock);
	if (paused != g_latest.paused)
	{
		g_latest.paused = paused;
		++g_latest.sequence;
		if (g_count == CAPACITY)
			g_overflow = true;
		else if (!g_overflow)
		{
			g_queue[(g_head + g_count) % CAPACITY] = g_latest;
			++g_count;
		}
	}
	LeaveCriticalSection(&g_lock);
	SetEvent(g_update);
}

void GroundElapsedTimePauseExit()
{
	if (g_thread)
	{
		SetEvent(g_stop);
		WaitForSingleObject(g_thread, INFINITE);
		CloseHandle(g_thread);
		g_thread = NULL;
	}
	if (g_stop) { CloseHandle(g_stop); g_stop = NULL; }
	if (g_update) { CloseHandle(g_update); g_update = NULL; }
	DeleteCriticalSection(&g_lock);
}
