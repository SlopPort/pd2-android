// Minimal Win32 shim so D2MOO (MIT) compiles with the Android NDK.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#define __stdcall
#define __fastcall
#define __cdecl
#define __thiscall
#define WINAPI
#define CALLBACK
#define APIENTRY
#define __declspec(x)
#define __forceinline inline __attribute__((always_inline))
typedef uint8_t BYTE; typedef uint16_t WORD; typedef uint32_t DWORD; typedef int32_t LONG;
typedef uint32_t UINT; typedef uint32_t ULONG; typedef int16_t SHORT; typedef uint16_t USHORT;
typedef uint8_t UCHAR; typedef char CHAR; typedef wchar_t WCHAR; typedef int INT;
typedef void* LPVOID; typedef const void* LPCVOID; typedef void* HANDLE; typedef void* HMODULE; typedef void* HINSTANCE;
typedef void* HWND; typedef char* LPSTR; typedef const char* LPCSTR; typedef wchar_t* LPWSTR; typedef const wchar_t* LPCWSTR;
typedef DWORD* LPDWORD; typedef intptr_t LPARAM; typedef uintptr_t WPARAM; typedef intptr_t LRESULT;
typedef uintptr_t UINT_PTR; typedef intptr_t INT_PTR; typedef uintptr_t DWORD_PTR; typedef uintptr_t ULONG_PTR; typedef intptr_t LONG_PTR;
typedef size_t SIZE_T; typedef int64_t LONGLONG; typedef uint64_t ULONGLONG;
#define __int64 long long
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define _countof(a) (sizeof(a)/sizeof((a)[0]))
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#include <strings.h>
// SAL annotations
#define _Analysis_assume_(x)
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
#define _In_z_
#define _Printf_format_string_
#define _Curr_
#define _In_reads_(x)
#define _In_reads_bytes_(x)
#define _Out_writes_(x)
#define _Out_writes_bytes_(x)
#define _Out_writes_z_(x)
#define _Success_(x)
#define _Ret_maybenull_
#define _Check_return_
#define _Pre_satisfies_(x)
#define _When_(a,b)
#define _Field_size_(x)
#define _Notnull_
#define _Maybenull_
#include <pthread.h>
// Same 24-byte size as the Win32 structure, so D2 structs that embed it keep their layout.
typedef struct { pthread_mutex_t m; char pad[24 - sizeof(pthread_mutex_t)]; } CRITICAL_SECTION, *LPCRITICAL_SECTION;
typedef struct { ULONG_PTR Internal, InternalHigh; DWORD Offset, OffsetHigh; HANDLE hEvent; } OVERLAPPED, *LPOVERLAPPED;
#define wsprintfA sprintf
#define wsprintf sprintf
typedef int32_t BOOL; typedef BYTE* LPBYTE; typedef void* LPSECURITY_ATTRIBUTES; typedef void* HRGN; typedef void* HDC; typedef void* HGDIOBJ; typedef void* HKEY;
typedef DWORD LCID; typedef intptr_t (*FARPROC)();
typedef struct { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME, *LPFILETIME;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT, *LPPAINTSTRUCT;
#include <algorithm>
#include <ctime>
#define _snprintf snprintf
#define _strcmpi strcasecmp
FILE* d2_fopen(const char* name, const char* mode); // resolves D2 "DATA\..." paths under the data root
#define fopen_s(pf, name, mode) ((*(pf) = d2_fopen((name), (mode))) ? 0 : 1)
inline void InitializeCriticalSection(LPCRITICAL_SECTION c){ pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(&c->m,&a);} 
inline void DeleteCriticalSection(LPCRITICAL_SECTION c){ pthread_mutex_destroy(&c->m);} 
inline void EnterCriticalSection(LPCRITICAL_SECTION c){ pthread_mutex_lock(&c->m);} 
inline void LeaveCriticalSection(LPCRITICAL_SECTION c){ pthread_mutex_unlock(&c->m);} 
inline DWORD GetTickCount(){ timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (DWORD)(t.tv_sec*1000 + t.tv_nsec/1000000);} 
inline void OutputDebugStringA(const char*){}
#define _Acquires_lock_(x)
#define _Releases_lock_(x)
#define _Requires_lock_held_(x)
#define _Acquires_exclusive_lock_(x)
#define _Releases_exclusive_lock_(x)
inline int strcpy_s(char* d, size_t n, const char* s){ strncpy(d,s,n); if(n) d[n-1]=0; return 0; }
template<size_t N> inline int strcpy_s(char (&d)[N], const char* s){ return strcpy_s(d,N,s); }
inline int strncpy_s(char* d, size_t n, const char* s, size_t c){ size_t k = c < n-1 ? c : n-1; strncpy(d,s,k); d[k]=0; return 0; }
template<size_t N> inline int strncpy_s(char (&d)[N], const char* s, size_t c){ return strncpy_s(d,N,s,c); }
template<class T> inline T InterlockedIncrement(volatile T* p){ return __sync_add_and_fetch(p,1);} 
template<class T> inline T InterlockedDecrement(volatile T* p){ return __sync_sub_and_fetch(p,1);} 
template<class T> inline T InterlockedExchange(volatile T* p, T v){ return __sync_lock_test_and_set(p,v);} 
inline BOOL IsBadCodePtr(FARPROC p){ return p == nullptr; }
// MSVC lets function pointers convert to void* implicitly in table initializers; clang does not.
struct D2AnyFnPtr { void* p; D2AnyFnPtr() : p(nullptr) {} D2AnyFnPtr(std::nullptr_t) : p(nullptr) {} D2AnyFnPtr(int) : p(nullptr) {} D2AnyFnPtr(long) : p(nullptr) {} template<class T> D2AnyFnPtr(T* f) : p((void*)f) {} template<class T> operator T*() const { return (T*)p; } bool operator!() const { return !p; } };
inline char* _itoa(int v, char* b, int r){ if(r==16) sprintf(b,"%x",v); else sprintf(b,"%d",v); return b; }
inline char* _ultoa(unsigned long v, char* b, int r){ if(r==16) sprintf(b,"%lx",v); else sprintf(b,"%lu",v); return b; }
inline void Sleep(DWORD ms){ timespec t{(time_t)(ms/1000),(long)(ms%1000)*1000000L}; nanosleep(&t,nullptr); }

// sprintf_s: both the explicit-size and the array forms used by D2MOO.
#include <cstdarg>
inline int sprintf_s(char* b, size_t n, const char* f, ...) { va_list a; va_start(a, f); int r = vsnprintf(b, n, f, a); va_end(a); return r; }
template<size_t N> inline int sprintf_s(char (&b)[N], const char* f, ...) { va_list a; va_start(a, f); int r = vsnprintf(b, N, f, a); va_end(a); return r; }
typedef union { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct _SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME, *LPSYSTEMTIME;
inline void GetLocalTime(LPSYSTEMTIME t) { time_t now = time(nullptr); struct tm l; localtime_r(&now, &l); t->wYear = (WORD)(l.tm_year + 1900); t->wMonth = (WORD)(l.tm_mon + 1); t->wDayOfWeek = (WORD)l.tm_wday; t->wDay = (WORD)l.tm_mday; t->wHour = (WORD)l.tm_hour; t->wMinute = (WORD)l.tm_min; t->wSecond = (WORD)l.tm_sec; t->wMilliseconds = 0; }
inline char* _strrev(char* s) { size_t n = strlen(s); for (size_t i = 0; i < n / 2; ++i) { char c = s[i]; s[i] = s[n - 1 - i]; s[n - 1 - i] = c; } return s; }
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* v) { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); v->QuadPart = (LONGLONG)t.tv_sec * 1000000000LL + t.tv_nsec; return TRUE; }
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* v) { v->QuadPart = 1000000000LL; return TRUE; }
template<class T> inline T InterlockedCompareExchange(volatile T* p, T exchange, T comparand) { return __sync_val_compare_and_swap(p, comparand, exchange); }
