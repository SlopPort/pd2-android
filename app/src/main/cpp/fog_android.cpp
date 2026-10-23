// Android implementation of the Fog.dll functions D2Common needs.
// D2MOO ships Fog only as empty stubs, so the txt table parser, the code/name
// linkers, memory and error reporting are written here from the behaviour of
// D2 1.10 Fog: tab separated tables, a header row naming the columns, rows
// whose first cell is "Expansion" skipped, unresolved links stored as -1.
#include <Windows.h>
#include <D2BasicTypes.h>
#include <Fog.h>
#include <android/log.h>
#include <cmath>
#include <cstdarg>
#include <string>
#include <unordered_map>
#include <vector>

#define LOG_TAG "D2Native"

// ---------------------------------------------------------------- errors

void __cdecl FOG_DisplayAssert(const char* szMsg, const char* szFile, int nLine) {
	__android_log_print(ANDROID_LOG_FATAL, LOG_TAG, "ASSERT %s (%s:%d)", szMsg, szFile, nLine);
	fprintf(stderr, "ASSERT %s (%s:%d)\n", szMsg, szFile, nLine);
	// D2 code follows an assert with exit(-1); trapping here leaves a tombstone with the stack instead.
	__builtin_trap();
}
void __cdecl FOG_DisplayHalt(const char* szMsg, const char* szFile, int nLine) {
	__android_log_print(ANDROID_LOG_FATAL, LOG_TAG, "HALT %s (%s:%d)", szMsg, szFile, nLine);
	fprintf(stderr, "HALT %s (%s:%d)\n", szMsg, szFile, nLine);
	abort();
}
void __cdecl FOG_DisplayWarning(const char* szMsg, const char* szFile, int nLine) {
	__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "WARN %s (%s:%d)", szMsg, szFile, nLine);
}
void __cdecl FOG_Trace(const char* szFormat, ...) {
	va_list va; va_start(va, szFormat);
	__android_log_vprint(ANDROID_LOG_DEBUG, LOG_TAG, szFormat, va);
	va_end(va);
}
const char* __cdecl FOG_csprintf(char* szDest, const char* szFormat, ...) {
	va_list va; va_start(va, szFormat);
	vsprintf(szDest, szFormat, va);
	va_end(va);
	return szDest;
}
void __fastcall FOG_10050_EnterCriticalSection(CRITICAL_SECTION* pCriticalSection, int) { EnterCriticalSection(pCriticalSection); }
int __fastcall FOG_IsExpansion() { return TRUE; }

// ---------------------------------------------------------------- memory

void* __fastcall FOG_Alloc(int nSize, const char*, int, int) { return malloc(nSize > 0 ? nSize : 1); }
void __fastcall FOG_Free(void* pFree, const char*, int, int) { free(pFree); }
void* __fastcall FOG_AllocPool(void*, int nSize, const char*, int, int) { return malloc(nSize > 0 ? nSize : 1); }
void __fastcall FOG_FreePool(void*, void* pFree, const char*, int, int) { free(pFree); }
void* __fastcall FOG_ReallocPool(void*, void* pMemory, int nSize, const char*, int, int) { return realloc(pMemory, nSize > 0 ? nSize : 1); }

// ---------------------------------------------------------------- trigonometry (512 steps per turn)

static float gCos[512], gSin[512];
static bool gTrigReady = false;
static void initTrig() {
	for (int i = 0; i < 512; ++i) { gCos[i] = (float)cos(i * M_PI * 2.0 / 512.0); gSin[i] = (float)sin(i * M_PI * 2.0 / 512.0); }
	gTrigReady = true;
}
float __stdcall FOG_10083_Cos_LUT(int16_t index) { if (!gTrigReady) initTrig(); return gCos[index & 511]; }
float __stdcall FOG_10084_Sin_LUT(int16_t index) { if (!gTrigReady) initTrig(); return gSin[index & 511]; }

// ---------------------------------------------------------------- linkers

namespace {
struct Linker {
	std::unordered_map<uint32_t, int> codes;
	std::vector<uint32_t> codeOf;          // index -> code
	std::unordered_map<std::string, int> names; // lower-case name -> index
	std::vector<std::string> nameOf;       // index -> original name
	int nRecords = 0;
};
std::string lower(const char* s) { std::string r = s ? s : ""; for (char& c : r) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return r; }
void addCode(Linker* l, uint32_t code, int index) {
	if (!l) return;
	if (!l->codes.count(code)) l->codes[code] = index;
	if ((int)l->codeOf.size() <= index) l->codeOf.resize(index + 1, 0);
	l->codeOf[index] = code;
	if (index >= l->nRecords) l->nRecords = index + 1;
}
void addName(Linker* l, const char* name, int index) {
	if (!l) return;
	std::string k = lower(name);
	if (!l->names.count(k)) l->names[k] = index;
	if ((int)l->nameOf.size() <= index) l->nameOf.resize(index + 1);
	l->nameOf[index] = name;
	if (index >= l->nRecords) l->nRecords = index + 1;
}
}

void* __stdcall FOG_AllocLinker(const char*, int) { return new Linker(); }
void __stdcall FOG_FreeLinker(void* pLinker) { delete (Linker*)pLinker; }

int __stdcall FOG_GetLinkIndex(void* pLink, uint32_t dwCode, BOOL bLogError) {
	Linker* l = (Linker*)pLink;
	if (l) { auto it = l->codes.find(dwCode); if (it != l->codes.end()) return it->second; }
	if (bLogError) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "link code %.4s not found", (const char*)&dwCode);
	return -1;
}

int __stdcall FOG_GetStringFromLinkIndex(void* pLinker, int nIndex, char* szString) {
	Linker* l = (Linker*)pLinker;
	if (!l || nIndex < 0 || nIndex >= (int)l->codeOf.size()) { if (szString) szString[0] = 0; return 0; }
	uint32_t c = l->codeOf[nIndex];
	memcpy(szString, &c, 4); szString[4] = 0;
	for (int i = 3; i >= 0 && szString[i] == ' '; --i) szString[i] = 0;
	return 1;
}

// Adds an item code to the code table with the next free index.
int __stdcall FOG_10215(void* pBin, int a2) { Linker* l = (Linker*)pBin; if (!l) return -1; int n = l->nRecords; addCode(l, (uint32_t)a2, n); return n; }
// Adds a name with the next free index.
int __stdcall FOG_10216_AddRecordToLinkingTable(void* pBin, const char* a2) { Linker* l = (Linker*)pBin; if (!l) return -1; int n = l->nRecords; addName(l, a2, n); return n; }

int __stdcall FOG_GetRowFromTxt(void* pBin, char* szText, int) {
	Linker* l = (Linker*)pBin;
	if (!l || !szText) return -1;
	auto it = l->names.find(lower(szText));
	return it == l->names.end() ? -1 : it->second;
}

char* __stdcall FOG_10255(void* pLinker, int nId, int) {
	static char empty[1] = {0};
	Linker* l = (Linker*)pLinker;
	if (!l || nId < 0 || nId >= (int)l->nameOf.size()) return empty;
	return (char*)l->nameOf[nId].c_str();
}

// ---------------------------------------------------------------- txt tables

namespace {
struct TxtTable {
	std::vector<std::string> header;
	std::vector<std::vector<char*>> rows;
	char* text = nullptr;
};
}

D2BinFileStrc* __stdcall FOG_CreateBinFile(void* pDataBuffer, int nBufferSize) {
	TxtTable* t = new TxtTable();
	t->text = (char*)malloc(nBufferSize + 1);
	memcpy(t->text, pDataBuffer, nBufferSize);
	t->text[nBufferSize] = 0;
	char* p = t->text;
	bool first = true;
	while (*p) {
		char* line = p;
		while (*p && *p != '\n') ++p;
		if (*p) *p++ = 0;
		size_t n = strlen(line);
		if (n && line[n - 1] == '\r') line[--n] = 0;
		std::vector<char*> cells;
		char* c = line;
		for (;;) {
			char* tab = strchr(c, '\t');
			if (tab) *tab = 0;
			// Excel exports may quote cells.
			size_t k = strlen(c);
			if (k >= 2 && c[0] == '"' && c[k - 1] == '"') { c[k - 1] = 0; ++c; }
			cells.push_back(c);
			if (!tab) break;
			c = tab + 1;
		}
		if (first) { for (char* h : cells) t->header.push_back(h); first = false; continue; }
		if (n == 0) continue;
		if (!strcasecmp(cells[0], "Expansion")) continue;
		t->rows.push_back(cells);
	}
	D2BinFileStrc* b = (D2BinFileStrc*)calloc(1, sizeof(D2BinFileStrc));
	b->pDataBuffer = (uint8_t*)t;
	b->nRecordCount = (int32_t)t->rows.size();
	b->nCellCount = (int32_t)t->header.size();
	return b;
}

void __stdcall FOG_FreeBinFile(D2BinFileStrc* pBinFile) {
	if (!pBinFile) return;
	TxtTable* t = (TxtTable*)pBinFile->pDataBuffer;
	if (t) { free(t->text); delete t; }
	free(pBinFile);
}

int __stdcall FOG_GetRecordCountFromBinFile(D2BinFileStrc* pBinFile) { return pBinFile ? pBinFile->nRecordCount : 0; }

static uint32_t codeFromString(const char* s) {
	char c[4] = {' ', ' ', ' ', ' '};
	for (int i = 0; i < 4 && s[i]; ++i) c[i] = s[i];
	uint32_t v; memcpy(&v, c, 4); return v;
}

typedef uint16_t(__fastcall* KeyToWordFn)(char*);
typedef void(__fastcall* CustomLinkFn)(char* pSrc, void* pRecord, int nOffset, int nPosition, int nTxtRow, int nTxtColumn);

void __stdcall FOG_10207(D2BinFileStrc* pBinFile, D2BinFieldStrc* pBinField, void* pTxt, int nRecordCount, int nRecordSize) {
	TxtTable* t = (TxtTable*)pBinFile->pDataBuffer;
	std::vector<int> columns;
	for (D2BinFieldStrc* f = pBinField; f->nFieldType != TXTFIELD_NONE || (f->szFieldName && strcasecmp(f->szFieldName, "end")); ++f) {
		if (f->nFieldType == TXTFIELD_NONE) { columns.push_back(-1); continue; }
		int col = -1;
		for (size_t i = 0; i < t->header.size(); ++i) if (!strcasecmp(t->header[i].c_str(), f->szFieldName)) { col = (int)i; break; }
		columns.push_back(col);
	}
	// Pass 0 registers the keys (codes/names) of every row, pass 1 fills the fields, so rows may
	// refer to rows further down the same table.
	for (int pass = 0; pass < 2; ++pass)
	for (int row = 0; row < nRecordCount && row < (int)t->rows.size(); ++row) {
		uint8_t* rec = (uint8_t*)pTxt + (size_t)row * nRecordSize;
		std::vector<char*>& cells = t->rows[row];
		for (size_t fi = 0; fi < columns.size(); ++fi) {
			D2BinFieldStrc* f = &pBinField[fi];
			int col = columns[fi];
			if (col < 0) continue;
			static char emptyCell[1] = {0};
			char* cell = col < (int)cells.size() ? cells[col] : emptyCell;
			bool empty = cell[0] == 0;
			uint8_t* dst = rec + f->nFieldOffset;
			Linker* link = f->pLinkField.p ? *(Linker**)f->pLinkField.p : nullptr;
			bool keyField = f->nFieldType == TXTFIELD_ASCIITOCODE || f->nFieldType == TXTFIELD_NAMETOINDEX || f->nFieldType == TXTFIELD_NAMETOINDEX2;
			if (keyField != (pass == 0)) { if (keyField) { if (f->nFieldType == TXTFIELD_ASCIITOCODE) { uint32_t v = codeFromString(cell); memcpy(dst, &v, 4); } } continue; }
			switch (f->nFieldType) {
			case TXTFIELD_ASCII: {
				int len = f->nFieldLength;
				int n = (int)strlen(cell); if (n > len - 1) n = len - 1; if (n < 0) n = 0;
				memcpy(dst, cell, n); dst[n] = 0;
				break;
			}
			case TXTFIELD_DWORD: { int32_t v = atoi(cell); memcpy(dst, &v, 4); break; }
			case TXTFIELD_DWORD2: { uint32_t v = (uint32_t)strtoul(cell, nullptr, 10); memcpy(dst, &v, 4); break; }
			case TXTFIELD_WORD: { int16_t v = (int16_t)atoi(cell); memcpy(dst, &v, 2); break; }
			case TXTFIELD_BYTE: case TXTFIELD_UNKNOWN2: case TXTFIELD_BYTE2: { *dst = (uint8_t)atoi(cell); break; }
			case TXTFIELD_BIT: {
				uint32_t v; memcpy(&v, dst, 4);
				if (atoi(cell)) v |= (1u << f->nFieldLength); else v &= ~(1u << f->nFieldLength);
				memcpy(dst, &v, 4);
				break;
			}
			case TXTFIELD_RAW: { uint32_t v = codeFromString(cell); memcpy(dst, &v, 4); break; }
			case TXTFIELD_ASCIITOCODE: {
				// Record code; the link index continues across tables sharing a linker (weapons/armor/misc).
				uint32_t v = codeFromString(cell); memcpy(dst, &v, 4);
				if (link) addCode(link, v, link->nRecords);
				break;
			}
			case TXTFIELD_UNKNOWN3: { // code -> dword link index
				int idx = empty ? -1 : FOG_GetLinkIndex(link, codeFromString(cell), FALSE);
				memcpy(dst, &idx, 4);
				break;
			}
			case TXTFIELD_NAMETOINDEX: case TXTFIELD_NAMETOINDEX2: if (link) addName(link, cell, link->nRecords); break;
			case TXTFIELD_CODETOBYTE: case TXTFIELD_CODETOWORD: {
				// Matches the original bins: an empty cell is 0xFF as a byte but 0 as a word.
				int idx = FOG_GetLinkIndex(link, codeFromString(cell), FALSE);
				if (empty) idx = f->nFieldType == TXTFIELD_CODETOBYTE ? -1 : 0;
				if (f->nFieldType == TXTFIELD_CODETOBYTE) *dst = (uint8_t)idx; else { int16_t v = (int16_t)idx; memcpy(dst, &v, 2); }
				break;
			}
			case TXTFIELD_NAMETODWORD: case TXTFIELD_NAMETOWORD: case TXTFIELD_NAMETOWORD2: {
				int idx = empty ? -1 : FOG_GetRowFromTxt(link, cell, 0);
				if (f->nFieldType == TXTFIELD_NAMETODWORD) memcpy(dst, &idx, 4); else { int16_t v = (int16_t)idx; memcpy(dst, &v, 2); }
				break;
			}
			case TXTFIELD_KEYTOWORD: {
				uint16_t v = 0;
				if (f->pLinkField.p) v = ((KeyToWordFn)f->pLinkField.p)(cell);
				memcpy(dst, &v, 2);
				break;
			}
			case TXTFIELD_CUSTOMLINK: case TXTFIELD_CALCTODWORD:
				if (f->pLinkField.p) ((CustomLinkFn)f->pLinkField.p)(empty ? nullptr : cell, rec, f->nFieldOffset, f->nFieldLength, row, col);
				break;
			default:
				break;
			}
		}
	}
}
