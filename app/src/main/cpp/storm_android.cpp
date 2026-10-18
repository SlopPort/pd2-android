// Android replacements for the parts of Storm/D2Hell that D2Common needs.
// Game files are read from a plain directory tree (extracted from the MPQs),
// looked up case-insensitively with '\' converted to '/'.
#include <Windows.h>
#include <D2BasicTypes.h>
#include <Archive.h>
#include <Storm.h>
#include <File.h>
#include <string>
#include <android/log.h>

static std::string gRoot = "/data/local/tmp/d2data";
extern "C" void d2native_set_data_root(const char* root) { gRoot = root; }

// When set (by the JNI bridge), files come from the app's MPQ installation instead of gRoot.
typedef void* (*D2ReadHook)(const char* path, size_t* size);
static D2ReadHook gReadHook;
extern "C" void d2native_set_read_hook(D2ReadHook hook) { gReadHook = hook; }

static std::string resolve(const char* path) {
	std::string p = path;
	for (char& c : p) { if (c == '\\') c = '/'; else if (c >= 'A' && c <= 'Z') c = (char)(c + 32); }
	if (!p.empty() && p[0] == '/') p.erase(0, 1);
	return gRoot + "/" + p;
}

BOOL __fastcall ARCHIVE_OpenFile(HD2ARCHIVE, const char* szFilePath, HSFILE* phFile, BOOL bSkipLog) {
	FILE* f = nullptr;
	if (gReadHook) {
		size_t n = 0;
		void* data = gReadHook(szFilePath, &n);
		if (data) { f = fmemopen(nullptr, n ? n : 1, "w+b"); if (f) { fwrite(data, 1, n, f); rewind(f); } free(data); }
	} else {
		f = fopen(resolve(szFilePath).c_str(), "rb");
	}
	if (!f && !bSkipLog) __android_log_print(ANDROID_LOG_WARN, "D2Native", "missing file %s", szFilePath);
	*phFile = (HSFILE)f;
	return f != nullptr;
}

void __fastcall ARCHIVE_CloseFile(HD2ARCHIVE, HSFILE hFile) { if (hFile) fclose((FILE*)hFile); }

void* __fastcall ARCHIVE_AllocateBufferAndReadFile(HD2ARCHIVE, const char* szFilePath, size_t* pdwBytesWritten, const char*, int) {
	if (gReadHook) {
		size_t n = 0;
		void* data = gReadHook(szFilePath, &n);
		if (!data) __android_log_print(ANDROID_LOG_ERROR, "D2Native", "missing file %s", szFilePath);
		if (pdwBytesWritten) *pdwBytesWritten = data ? n : 0;
		return data;
	}
	FILE* f = fopen(resolve(szFilePath).c_str(), "rb");
	if (!f) {
		__android_log_print(ANDROID_LOG_ERROR, "D2Native", "missing file %s", szFilePath);
		if (pdwBytesWritten) *pdwBytesWritten = 0;
		return nullptr;
	}
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	// Fog's txt parser expects a terminator after the data.
	char* buf = (char*)calloc(1, n + 4);
	fread(buf, 1, n, f); fclose(f);
	if (pdwBytesWritten) *pdwBytesWritten = (size_t)n;
	return buf;
}

int32_t __stdcall SStrCmpI(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }

int __stdcall SUniConvertUTF8to16(wchar_t* pBuffer, int nBufferCount, const char* szSrc, int nSrcCount, int* pOutLength, int) {
	// D2 wchar_t is 16-bit on Windows; this only fills ASCII/Latin-1 text into the buffer it is given.
	int o = 0;
	for (int i = 0; szSrc[i] && (nSrcCount < 0 || i < nSrcCount) && o < nBufferCount - 1; ++i) pBuffer[o++] = (unsigned char)szSrc[i];
	if (nBufferCount > 0) pBuffer[o] = 0;
	if (pOutLength) *pOutLength = o;
	return o;
}

size_t __cdecl FileLockAndWrite(void* Buffer, size_t ElementSize, size_t ElementCount, FILE* Stream) { return fwrite(Buffer, ElementSize, ElementCount, Stream); }

FILE* d2_fopen(const char* name, const char* mode) {
	if (strchr(name, '\\')) return fopen(resolve(name).c_str(), mode);
	return fopen(name, mode);
}

static std::string gSaveDir = "/data/local/tmp/d2save/";
extern "C" void d2native_set_save_dir(const char* dir) { gSaveDir = dir; }
const char* d2native_save_dir() { return gSaveDir.c_str(); }
