#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "GroundElapsedTimeHttp.h"

#ifndef GET_HTTP_PORT
#define GET_HTTP_PORT 8765
#endif

namespace {
const ULONGLONG STALE_MS = 1500;
const DWORD IO_TIMEOUT_MS = 500;
SRWLOCK lock = SRWLOCK_INIT;
struct Snapshot {
	double get = 0, accel = 1, simt = 0;
	ULONGLONG tick = 0;
	unsigned long long seq = 0;
	const void *vessel = nullptr;
	long offset = 0;
	bool active = false, paused = false, valid = false, awaiting = true;
};
Snapshot state;
HANDLE stopEvent = nullptr, thread = nullptr;
volatile LONG pendingError = 0;

void report(const char *message, int error)
{
	char text[256];
	_snprintf_s(text, sizeof(text), _TRUNCATE, "GroundElapsedTime HTTP: %s (%d)\n", message, error);
	OutputDebugStringA(text);
	InterlockedExchange(&pendingError, error ? error : -1);
}

void formatSnapshot(char *body, size_t size)
{
	AcquireSRWLockShared(&lock);
	Snapshot s = state;
	ReleaseSRWLockShared(&lock);
	ULONGLONG age = GetTickCount64() - s.tick;
	bool available = s.active && s.valid && !s.awaiting && (s.paused || age <= STALE_MS);
	bool running = available && !s.paused && std::fabs(s.accel - 1.0) < 0.01;
	if (available)
	{
		double value = s.get;
		if (running) value += age * 0.001 * s.accel;
		_snprintf_s(body, size, _TRUNCATE,
			"{\"get\":%.6f,\"accel\":%.6f,\"running\":%s,\"paused\":%s,\"seq\":%llu,\"offset\":0}",
			value, s.accel, running ? "true" : "false", s.paused ? "true" : "false", s.seq);
	}
	else
		_snprintf_s(body, size, _TRUNCATE,
			"{\"get\":null,\"accel\":%.6f,\"running\":false,\"paused\":null,\"seq\":%llu,\"offset\":0}",
			s.accel, s.seq);
	// JSON requires a decimal point even if Orbiter's locale uses a comma.
	for (char *p = body; *p; ++p) if (*p == ',') {
		if (p > body && p[1] >= '0' && p[1] <= '9' && p[-1] >= '0' && p[-1] <= '9') *p = '.';
	}
}

int waitSocket(SOCKET s, bool write, ULONGLONG deadline)
{
	while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0 && GetTickCount64() < deadline)
	{
		fd_set set; FD_ZERO(&set); FD_SET(s, &set);
		timeval tv = { 0, 50000 };
		int result = select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv);
		if (result > 0) return 1;
		if (result == SOCKET_ERROR) return -1;
	}
	return 0;
}

bool hasValidHost(const char *request)
{
	char expected[64];
	_snprintf_s(expected, sizeof(expected), _TRUNCATE, "127.0.0.1:%d", GET_HTTP_PORT);
	const char *line = strstr(request, "\r\n");
	bool found = false;
	while (line && line[2] && line[2] != '\r')
	{
		line += 2;
		const char *end = strstr(line, "\r\n");
		if (!end) return false;
		if (end - line >= 5 && _strnicmp(line, "Host:", 5) == 0)
		{
			if (found) return false;
			line += 5;
			while (line < end && (*line == ' ' || *line == '\t')) ++line;
			while (end > line && (end[-1] == ' ' || end[-1] == '\t')) --end;
			if ((size_t)(end - line) != strlen(expected) || _strnicmp(line, expected, end - line) != 0)
				return false;
			found = true;
			line = strstr(line, "\r\n");
		}
		else line = end;
	}
	return found;
}

void serve(SOCKET client)
{
	u_long nonblocking = 1;
	if (ioctlsocket(client, FIONBIO, &nonblocking) != 0) return;
	ULONGLONG deadline = GetTickCount64() + IO_TIMEOUT_MS;
	char request[4096] = {};
	int used = 0;
	while (used < (int)sizeof(request) - 1 && !strstr(request, "\r\n\r\n"))
	{
		if (waitSocket(client, false, deadline) <= 0) return;
		int n = recv(client, request + used, (int)sizeof(request) - 1 - used, 0);
		if (n <= 0) return;
		used += n;
		request[used] = 0;
	}
	if (!strstr(request, "\r\n\r\n")) return;
	bool valid = (strncmp(request, "GET / HTTP/1.1\r\n", 16) == 0 ||
		strncmp(request, "GET / HTTP/1.0\r\n", 16) == 0) && hasValidHost(request);
	char body[512];
	if (valid) formatSnapshot(body, sizeof(body));
	else strcpy_s(body, "Bad request");
	char response[1024];
	int length = _snprintf_s(response, sizeof(response), _TRUNCATE,
		"HTTP/1.1 %s\r\nContent-Type: %s\r\nCache-Control: no-store\r\n"
		"Connection: close\r\nContent-Length: %zu\r\n\r\n%s",
		valid ? "200 OK" : "400 Bad Request", valid ? "application/json" : "text/plain",
		strlen(body), body);
	if (length < 0) return;
	int sent = 0;
	while (sent < length && waitSocket(client, true, deadline) > 0)
	{
		int n = send(client, response + sent, length - sent, 0);
		if (n <= 0) return;
		sent += n;
	}
}

DWORD WINAPI serverThread(LPVOID)
{
	WSADATA wsa;
	int error = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (error) { report("WSAStartup failed", error); return 1; }
	SOCKET listener = INVALID_SOCKET;
	bool reported = false;
	while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0 && listener == INVALID_SOCKET)
	{
		SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		BOOL exclusive = TRUE;
		u_long nonblocking = 1;
		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_port = htons(GET_HTTP_PORT);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (candidate != INVALID_SOCKET &&
			ioctlsocket(candidate, FIONBIO, &nonblocking) == 0 &&
			setsockopt(candidate, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&exclusive, sizeof(exclusive)) == 0 &&
			bind(candidate, (sockaddr *)&address, sizeof(address)) == 0 &&
			listen(candidate, 8) == 0)
			listener = candidate;
		else
		{
			if (!reported) report("cannot bind/listen on 127.0.0.1 (port may already be in use)", WSAGetLastError());
			reported = true;
			if (candidate != INVALID_SOCKET) closesocket(candidate);
			WaitForSingleObject(stopEvent, 1000);
		}
	}
	if (listener == INVALID_SOCKET) { WSACleanup(); return 0; }
	while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
	{
		int ready = waitSocket(listener, false, GetTickCount64() + 100);
		if (ready < 0) { report("listener select failed", WSAGetLastError()); break; }
		if (!ready) continue;
		SOCKET client = accept(listener, nullptr, nullptr);
		if (client != INVALID_SOCKET) { serve(client); closesocket(client); }
		else if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			report("accept failed", WSAGetLastError());
			break;
		}
	}
	closesocket(listener);
	WSACleanup();
	return 0;
}
}

void GroundElapsedTimeHttpInit(bool paused)
{
	if (thread) return;
	InterlockedExchange(&pendingError, 0);
	AcquireSRWLockExclusive(&lock);
	unsigned long long seq = state.seq + 1;
	state = Snapshot();
	state.seq = seq;
	state.active = true;
	state.paused = paused;
	ReleaseSRWLockExclusive(&lock);
	stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (stopEvent) thread = CreateThread(nullptr, 0, serverThread, nullptr, 0, nullptr);
	if (!thread)
	{
		report("worker creation failed", GetLastError());
		if (stopEvent) CloseHandle(stopEvent);
		stopEvent = nullptr;
	}
}

int GroundElapsedTimeHttpTakeError()
{
	return (int)InterlockedExchange(&pendingError, 0);
}

void GroundElapsedTimeHttpExit()
{
	AcquireSRWLockExclusive(&lock);
	state.active = false;
	ReleaseSRWLockExclusive(&lock);
	if (thread)
	{
		SetEvent(stopEvent);
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
		thread = nullptr;
	}
	if (stopEvent) { CloseHandle(stopEvent); stopEvent = nullptr; }
}

void GroundElapsedTimeHttpPause(bool paused)
{
	AcquireSRWLockExclusive(&lock);
	if (state.active && state.paused != paused)
	{
		ULONGLONG now = GetTickCount64();
		if (paused && state.valid && !state.awaiting)
		{
			ULONGLONG age = now - state.tick;
			if (age > STALE_MS) state.valid = false;
			else if (std::fabs(state.accel - 1.0) < 0.01)
				state.get += age * 0.001 * state.accel;
			state.tick = now;
		}
		else if (paused && state.awaiting)
			state.valid = false;
		state.paused = paused;
		state.awaiting = !paused;
		++state.seq;
	}
	ReleaseSRWLockExclusive(&lock);
}

void GroundElapsedTimeHttpSample(double get, double accel, bool valid,
	const void *vessel, long offset, double simt)
{
	ULONGLONG now = GetTickCount64();
	valid = valid && std::isfinite(get) && std::isfinite(accel) && accel >= 0;
	AcquireSRWLockExclusive(&lock);
	if (state.active)
	{
		double expected = state.get + (simt - state.simt);
		bool jump = state.valid && valid &&
			(std::fabs(get - expected) > 2.0 || vessel != state.vessel || offset != state.offset);
		bool enteringOneX = std::fabs(state.accel - 1.0) >= 0.01 && std::fabs(accel - 1.0) < 0.01;
		bool recovered = state.valid && !state.paused && now - state.tick > STALE_MS;
		if (jump || enteringOneX || recovered || (!state.valid && valid)) ++state.seq;
		state.get = get;
		state.accel = valid ? accel : 1.0;
		state.simt = simt;
		state.tick = now;
		state.vessel = vessel;
		state.offset = offset;
		state.valid = valid;
		state.awaiting = false;
	}
	ReleaseSRWLockExclusive(&lock);
}
