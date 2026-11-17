#include <winsock2.h>
#include "Orbitersdk.h"
#include <cstdio>
#include <cstring>

extern "C" void opcDLLInit(HINSTANCE);
extern "C" void opcDLLExit(HINSTANCE);
static oapi::Module *module;
static VESSEL vessel;
static double met = 100, accel = 1, simt = 0;
static bool paused;
void oapiRegisterModule(oapi::Module *m) { module = m; }
bool oapiGetPause() { return paused; }
double oapiGetSimTime() { return simt; }
VESSEL *oapiGetFocusInterface() { return &vessel; }
double oapiGetTimeAcceleration() { return accel; }
bool ComputeGroundElapsedTime(VESSEL *, double &out) { out = met; return true; }
void FormatGroundElapsedTimeHMS(double t, char *out, int size)
{
	long s = (long)t, mag = s < 0 ? -s : s;
	sprintf_s(out, size, "%s%ld:%02ld:%02ld", s < 0 ? "-" : "", mag / 3600, (mag / 60) % 60, mag % 60);
}
void oapiWriteLogV(const char *, ...) {}
void GroundElapsedTimeMFDInit(HINSTANCE) {}
void GroundElapsedTimeMFDExit() {}

static int failures;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)
static const char *getName = "\\\\.\\pipe\\GroundElapsedTimeIntegrationTest";
static const char *pauseName = "\\\\.\\pipe\\GroundElapsedTimePauseIntegrationTest";

static HANDLE openPipe(const char *name)
{
	for (int i = 0; i < 300; ++i)
	{
		HANDLE pipe = CreateFileA(name, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (pipe != INVALID_HANDLE_VALUE) return pipe;
		Sleep(10);
	}
	return INVALID_HANDLE_VALUE;
}

static bool readPipe(HANDLE pipe, char *out, DWORD capacity)
{
	ULONGLONG deadline = GetTickCount64() + 3000;
	while (GetTickCount64() < deadline)
	{
		DWORD available = 0, read = 0;
		if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
		if (available)
		{
			if (!ReadFile(pipe, out, capacity - 1, &read, nullptr)) return false;
			out[read] = 0;
			return true;
		}
		Sleep(5);
	}
	return false;
}

static bool pollHttp(char *out, int capacity)
{
	SOCKET s = INVALID_SOCKET;
	for (int i = 0; i < 200; ++i)
	{
		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_port = htons(18766);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (connect(s, (sockaddr *)&address, sizeof(address)) == 0) break;
		closesocket(s);
		s = INVALID_SOCKET;
		Sleep(10);
	}
	if (s == INVALID_SOCKET) return false;
	DWORD timeout = 2000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
	const char req[] = "GET / HTTP/1.1\r\nHost: 127.0.0.1:18766\r\n\r\n";
	send(s, req, (int)strlen(req), 0);
	int used = 0, n;
	while (used < capacity - 1 && (n = recv(s, out + used, capacity - 1 - used, 0)) > 0) used += n;
	out[used] = 0;
	closesocket(s);
	return used > 0;
}

static bool pipeClosed(HANDLE pipe)
{
	ULONGLONG deadline = GetTickCount64() + 3000;
	while (GetTickCount64() < deadline)
	{
		DWORD available = 0, read = 0;
		char buffer[256];
		if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return true;
		if (available && !ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr)) return true;
		Sleep(5);
	}
	return false;
}

static bool httpListening()
{
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(18766);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bool connected = connect(s, (sockaddr *)&address, sizeof(address)) == 0;
	closesocket(s);
	return connected;
}

int main()
{
	WSADATA wsa;
	CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "client WSAStartup");
	opcDLLInit(nullptr);
	CHECK(module != nullptr, "module registration");
	module->clbkSimulationStart(oapi::Module::RENDER_NONE);
	simt = 1;
	module->clbkPreStep(simt, 1, 0);
	HANDLE getPipe = openPipe(getName), pausePipe = openPipe(pauseName);
	char msg[256], json[2048];
	CHECK(getPipe != INVALID_HANDLE_VALUE && pausePipe != INVALID_HANDLE_VALUE, "both clients connect");
	CHECK(readPipe(getPipe, msg, sizeof(msg)) && !strcmp(msg, "0:01:40\n1\n"), "GET initial pipe snapshot");
	CHECK(readPipe(pausePipe, msg, sizeof(msg)) && !strcmp(msg, "1 0 0\n"), "pause initial pipe snapshot");
	CHECK(pollHttp(json, sizeof(json)) && strstr(json, "\"running\":true") && strstr(json, "\"paused\":false"), "HTTP while both pipes connected");

	met = 101; accel = 60; simt = 2;
	module->clbkPreStep(simt, 1, 0);
	CHECK(readPipe(getPipe, msg, sizeof(msg)) && !strcmp(msg, "0:01:41\n60\n"), "60x reaches speech client");
	CHECK(pollHttp(json, sizeof(json)) && strstr(json, "\"accel\":60.000000") && strstr(json, "\"running\":false"), "HTTP reflects 60x");
	paused = true;
	module->clbkPause(true);
	CHECK(readPipe(pausePipe, msg, sizeof(msg)) && !strcmp(msg, "1 1 1\n"), "pause transition reaches client");
	CHECK(pollHttp(json, sizeof(json)) && strstr(json, "\"paused\":true"), "HTTP reflects pause");
	paused = false;
	module->clbkPause(false);
	CHECK(readPipe(pausePipe, msg, sizeof(msg)) && !strcmp(msg, "1 2 0\n"), "resume transition reaches client");
	CHECK(pollHttp(json, sizeof(json)) && strstr(json, "\"get\":null"), "HTTP waits for resumed sample");
	met = 102; accel = 1; simt = 3;
	module->clbkPreStep(simt, 1, 0);
	CHECK(readPipe(getPipe, msg, sizeof(msg)) && !strcmp(msg, "0:01:42\n1\n"), "1x reaches speech client");
	CHECK(pollHttp(json, sizeof(json)) && strstr(json, "\"running\":true"), "HTTP resyncs at 1x");

	CloseHandle(getPipe);
	CloseHandle(pausePipe);
	met = 103; simt = 4;
	module->clbkPreStep(simt, 1, 0);
	CHECK(pollHttp(json, sizeof(json)) && strstr(json, "\"running\":true"), "HTTP independent of disconnected pipes");
	getPipe = openPipe(getName);
	pausePipe = openPipe(pauseName);
	CHECK(getPipe != INVALID_HANDLE_VALUE && pausePipe != INVALID_HANDLE_VALUE, "pipe clients reconnect");
	CHECK(readPipe(getPipe, msg, sizeof(msg)) && !strcmp(msg, "0:01:43\n1\n"), "reconnected GET snapshot");
	CHECK(readPipe(pausePipe, msg, sizeof(msg)) && !strcmp(msg, "1 2 0\n"), "reconnected pause snapshot");
	module->clbkSimulationEnd();
	CHECK(pipeClosed(getPipe) && pipeClosed(pausePipe), "both pipes close at shutdown");
	CHECK(!httpListening(), "HTTP listener closes at shutdown");
	CloseHandle(getPipe);
	CloseHandle(pausePipe);

	met = 200; simt = 0;
	module->clbkSimulationStart(oapi::Module::RENDER_NONE);
	module->clbkPreStep(simt, 0, 0);
	getPipe = openPipe(getName);
	pausePipe = openPipe(pauseName);
	CHECK(readPipe(getPipe, msg, sizeof(msg)) && !strcmp(msg, "0:03:20\n1\n"), "GET after restart");
	CHECK(readPipe(pausePipe, msg, sizeof(msg)) && !strcmp(msg, "1 0 0\n"), "pause counter after restart");
	CHECK(pollHttp(json, sizeof(json)) && strstr(json, "\"get\":200.") && strstr(json, "\"running\":true"), "HTTP after restart");
	module->clbkSimulationEnd();
	CloseHandle(getPipe);
	CloseHandle(pausePipe);
	opcDLLExit(nullptr);
	WSACleanup();
	printf("Concurrent transports: %s\n", failures ? "FAILED" : "PASSED");
	return failures ? 1 : 0;
}
