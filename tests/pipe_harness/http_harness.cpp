#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "GroundElapsedTimeHttp.h"

static int failures;
#define CHECK(c, text) do { if (!(c)) { printf("FAIL: %s\n", text); ++failures; } } while (0)

static SOCKET connectServer()
{
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(18765);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(s, (sockaddr *)&address, sizeof(address)) != 0) { closesocket(s); return INVALID_SOCKET; }
	DWORD timeout = 2000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
	return s;
}

static bool request(char *out, size_t size, const char *path = "/", const char *host = "127.0.0.1:18765")
{
	SOCKET s = INVALID_SOCKET;
	for (int i = 0; i < 250 && s == INVALID_SOCKET; ++i) { s = connectServer(); if (s == INVALID_SOCKET) Sleep(10); }
	if (s == INVALID_SOCKET) return false;
	char req[256];
	sprintf_s(req, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", path, host);
	send(s, req, (int)strlen(req), 0);
	int used = 0, n;
	while (used < (int)size - 1 && (n = recv(s, out + used, (int)size - 1 - used, 0)) > 0) used += n;
	out[used] = 0;
	closesocket(s);
	return used != 0;
}

static double value(const char *text, const char *name)
{
	const char *p = strstr(text, name);
	return p ? atof(p + strlen(name)) : -1;
}

int main()
{
	WSADATA wsa;
	CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "client WSAStartup");
	char out[2048];
	SOCKET blocker = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	BOOL exclusive = TRUE;
	setsockopt(blocker, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&exclusive, sizeof(exclusive));
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(18765);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(blocker, (sockaddr *)&address, sizeof(address)) == 0 && listen(blocker, 1) == 0, "reserve port");
	GroundElapsedTimeHttpInit(false);
	int conflict = 0;
	for (int i = 0; i < 100 && !conflict; ++i) { Sleep(10); conflict = GroundElapsedTimeHttpTakeError(); }
	CHECK(conflict != 0, "port conflict reported");
	closesocket(blocker);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"get\":null") && strstr(out, "\"paused\":null"), "initial unavailable");
	GroundElapsedTimeHttpSample(100.25, 1, true, (void *)1, 0, 0);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"running\":true") && strstr(out, "\"paused\":false"), "first valid running sample");
	double first = value(out, "\"get\":");
	double seq = value(out, "\"seq\":");
	Sleep(120);
	CHECK(request(out, sizeof(out)) && value(out, "\"get\":") > first + .07, "running GET extrapolates");
	GroundElapsedTimeHttpPause(true);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"paused\":true") && strstr(out, "\"running\":false"), "pause snapshot");
	double frozen = value(out, "\"get\":");
	Sleep(150);
	CHECK(request(out, sizeof(out)) && fabs(value(out, "\"get\":") - frozen) < .001, "paused GET freezes");
	GroundElapsedTimeHttpPause(false);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"get\":null"), "resume awaits a fresh sample");
	GroundElapsedTimeHttpSample(100.5, 60, true, (void *)1, 0, .25);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"accel\":60.000000") && strstr(out, "\"running\":false") && strstr(out, "\"paused\":false"), "60x state");
	Sleep(100);
	CHECK(request(out, sizeof(out)) && fabs(value(out, "\"get\":") - 100.5) < .001, "60x uses last actual sample");
	GroundElapsedTimeHttpPause(true);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"paused\":true") && fabs(value(out, "\"get\":") - 100.5) < .001, "pause at 60x preserves sample");
	GroundElapsedTimeHttpPause(false);
	GroundElapsedTimeHttpSample(101.5, 1, true, (void *)1, 0, 1.25);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"running\":true") && value(out, "\"seq\":") > seq, "return to 1x resync");
	seq = value(out, "\"seq\":");
	GroundElapsedTimeHttpSample(500, 1, true, (void *)1, 0, 1.5);
	CHECK(request(out, sizeof(out)) && value(out, "\"seq\":") > seq, "jump resync");
	seq = value(out, "\"seq\":");
	GroundElapsedTimeHttpSample(510, 1, true, (void *)1, 10, 1.5);
	CHECK(request(out, sizeof(out)) && value(out, "\"seq\":") > seq, "offset resync");
	seq = value(out, "\"seq\":");
	GroundElapsedTimeHttpSample(510, 1, true, (void *)2, 10, 1.5);
	CHECK(request(out, sizeof(out)) && value(out, "\"seq\":") > seq, "focus resync");
	GroundElapsedTimeHttpSample(0, 1, false, (void *)1, 0, 2);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"get\":null"), "unsupported source unavailable");
	GroundElapsedTimeHttpSample(10, 1, true, (void *)1, 0, 3);
	Sleep(1600);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"get\":null") && strstr(out, "\"paused\":null"), "stale state unavailable");
	seq = value(out, "\"seq\":");
	GroundElapsedTimeHttpSample(11.6, 1, true, (void *)1, 0, 4.6);
	CHECK(request(out, sizeof(out)) && value(out, "\"seq\":") > seq, "stale recovery resync");
	CHECK(request(out, sizeof(out), "/other") && strstr(out, "400 Bad Request"), "reject non-root path");
	CHECK(request(out, sizeof(out), "/", "example.com") && strstr(out, "400 Bad Request"), "reject DNS rebinding host");
	SOCKET stalled = connectServer();
	CHECK(stalled != INVALID_SOCKET, "connect stalled client");
	ULONGLONG start = GetTickCount64();
	GroundElapsedTimeHttpExit();
	CHECK(GetTickCount64() - start < 1000, "shutdown with stalled client");
	if (stalled != INVALID_SOCKET) closesocket(stalled);
	GroundElapsedTimeHttpInit(true);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"get\":null"), "restart clears old snapshot");
	GroundElapsedTimeHttpSample(-20, 1, true, (void *)1, 0, 0);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"paused\":true") && value(out, "\"get\":") == -20, "initial paused sample");
	Sleep(1600);
	CHECK(request(out, sizeof(out)) && strstr(out, "\"paused\":true") && value(out, "\"get\":") == -20, "paused sample does not expire");
	GroundElapsedTimeHttpExit();
	WSACleanup();
	printf("HTTP harness: %s\n", failures ? "FAILED" : "PASSED");
	return failures ? 1 : 0;
}
