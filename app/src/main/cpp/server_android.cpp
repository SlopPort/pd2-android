// What D2Game needs from Storm, Fog and D2Net beyond D2Common, for a single-player game on
// Android. Networking is not used: the app reads the server's units directly, so packets to the
// client and the client message queues are empty stubs.
#include <Windows.h>
#include <D2BasicTypes.h>
#include <Fog.h>
#include <Storm.h>
#include <File.h>
#include <D2Net.h>
#include <Server.h>
#include <android/log.h>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------- Storm

size_t __stdcall SStrLen(const char* string) { return string ? strlen(string) : 0; }
size_t __stdcall SStrCopy(char* dest, const char* source, size_t destsize) {
	if (!dest || !destsize) return 0;
	size_t n = source ? strnlen(source, destsize - 1) : 0;
	memcpy(dest, source, n); dest[n] = 0;
	return n;
}
BOOL __stdcall SRegLoadValue(const char*, const char*, uint32_t, DWORD*) { return FALSE; } // no registry settings
void* __stdcall SMemAlloc(int amount, const char*, int, uint32_t flags) {
	void* p = malloc(amount > 0 ? amount : 1);
	if (p && (flags & 8)) memset(p, 0, amount > 0 ? amount : 1); // SMEM_FLAG_ZEROMEMORY
	return p;
}
BOOL __stdcall SMemFree(void* location, const char*, int, uint32_t) { free(location); return TRUE; }
void* __stdcall SMemReAlloc(void* location, int amount, const char*, int, uint32_t) { return realloc(location, amount > 0 ? amount : 1); }
int __cdecl SErrDisplayErrorFmt(DWORD, const char* filename, int32_t linenumber, int32_t, UINT, char* Format, ...) {
	char msg[1024];
	va_list va; va_start(va, Format); vsnprintf(msg, sizeof msg, Format, va); va_end(va);
	__android_log_print(ANDROID_LOG_ERROR, "D2Native", "%s (%s:%d)", msg, filename, linenumber);
	return 0;
}

// ---------------------------------------------------------------- Fog

void __cdecl FOG_TraceF(const char*, const char* szFormat, ...) {
	va_list va; va_start(va, szFormat);
	__android_log_vprint(ANDROID_LOG_DEBUG, "D2Native", szFormat, va);
	va_end(va);
}
uint32_t __stdcall FOG_PopCount(void* pData, size_t dwSize) {
	uint32_t n = 0;
	for (size_t i = 0; i < dwSize; ++i) n += __builtin_popcount(((uint8_t*)pData)[i]);
	return n;
}
// Despite the name, callers use it as "index of the highest set bit, -1 for zero" (they clear that bit and loop while >= 0).
uint32_t __stdcall FOG_LeadingZeroesCount(uint32_t nValue) { return nValue ? 31 - __builtin_clz(nValue) : (uint32_t)-1; }
size_t __fastcall FOG_GetSavePath(char* pPathBuffer, size_t nBufferSize) {
	extern const char* d2native_save_dir();
	return SStrCopy(pPathBuffer, d2native_save_dir(), nBufferSize);
}
DWORD __cdecl FOG_GetMemoryUsage(void*) { return 0; }
void __stdcall FOG_Encode14BitsToString(uint16_t* p2Characters, uint32_t value) {
	// Two printable characters carrying 7 bits each.
	((uint8_t*)p2Characters)[0] = (uint8_t)((value & 0x7F) | 0x80);
	((uint8_t*)p2Characters)[1] = (uint8_t)(((value >> 7) & 0x7F) | 0x80);
}
void __cdecl FOG_CreateNewPoolSystem(void** pMemPoolSystem, const char*, uint32_t, uint32_t) { if (pMemPoolSystem) *pMemPoolSystem = nullptr; }
void __cdecl FOG_DestroyMemoryPoolSystem(void*) {}
uint16_t __stdcall FOG_ComputeStringCRC16(const char* szString) {
	uint16_t crc = 0xFFFF; // CRC-16/CCITT; only used for names and hash keys
	for (const uint8_t* p = (const uint8_t*)szString; p && *p; ++p) {
		crc ^= (uint16_t)(*p << 8);
		for (int i = 0; i < 8; ++i) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
	}
	return crc;
}
uint32_t __stdcall FOG_ComputeChecksum(void* pData, size_t dwSize) {
	uint32_t sum = 0;
	for (size_t i = 0; i < dwSize; ++i) sum = (sum << 1 | sum >> 31) + ((uint8_t*)pData)[i];
	return sum;
}
int32_t __fastcall FOG_10055_GetSyncTime() { return (int32_t)GetTickCount(); }

uint32_t CRC32_Compute(void* pData, size_t dwSize) {
	uint32_t crc = 0xFFFFFFFF;
	for (size_t i = 0; i < dwSize; ++i) {
		crc ^= ((uint8_t*)pData)[i];
		for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
	return ~crc;
}
size_t __cdecl FileLockAndRead(void* Buffer, size_t ElementSize, size_t ElementCount, FILE* Stream) { return fread(Buffer, ElementSize, ElementCount, Stream); }

// ---------------------------------------------------------------- D2Net / server (no network; queues are in loopback.cpp)

int32_t __stdcall SERVER_WSAGetLastError() { return 0; }
void __stdcall SERVER_GetIpAddressStringFromClientId(int32_t, char* szBuffer, int32_t nBufferSize) { SStrCopy(szBuffer, "127.0.0.1", (size_t)nBufferSize); }
int32_t __stdcall D2NET_10019(D2NET_Unk_Callback) { return 0; }
void __stdcall D2NET_10016(int32_t) {}
void __stdcall D2NET_10015(int32_t, const char*, int32_t) {}

// ---------------------------------------------------------------- D2Game pieces tied to the Windows task queue

struct D2TaskStrc; struct D2LinkStrc;
BOOL __fastcall D2Game_10042(D2TaskStrc*, int, D2LinkStrc*) { return FALSE; }
void __stdcall D2Game_10034_Return(int) {}
void __stdcall TASK_FreeAllQueueSlots() {}

// squall's own declaration (storm/Memory.hpp) takes an unsigned size.
void* SMemAlloc(uint32_t bytes, const char* filename, int32_t linenumber, uint32_t flags) { return SMemAlloc((int)bytes, filename, linenumber, flags); }

// squall's STORM_ASSERT (storm/Error.hpp).
static const char* gFatalFile; static int gFatalLine;
int32_t __stdcall SErrPrepareAppFatal(const char* filename, int32_t linenumber) { gFatalFile = filename; gFatalLine = linenumber; return 0; }
void SErrDisplayAppFatal(char* format, ...) {
	char msg[1024];
	va_list va; va_start(va, format); vsnprintf(msg, sizeof msg, format, va); va_end(va);
	__android_log_print(ANDROID_LOG_FATAL, "D2Native", "FATAL %s (%s:%d)", msg, gFatalFile ? gFatalFile : "?", gFatalLine);
	abort();
}
