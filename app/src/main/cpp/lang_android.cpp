// Android implementation of the D2Lang string table lookups (D2MOO ships them as stubs).
// Loads data/local/lng/eng/{string,expansionstring,patchstring}.tbl. Global ids follow D2:
// string.tbl from 0, patchstring.tbl from 10000, expansionstring.tbl from 20000; a key is
// searched in patch, then expansion, then base, as the game does.
#include <Windows.h>
#include <D2BasicTypes.h>
#include <D2Lang.h>
#include <D2Unicode.h>
#include <Archive.h>
#include <android/log.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct Table {
	int base = 0;
	std::vector<std::u16string> strings;              // by local index
	std::unordered_map<std::string, int> keys;         // key -> local index
};

std::vector<Table> gTables; // search order: patch, expansion, base
bool gLoaded = false;
std::u16string gEmpty;

std::u16string decode(const char* s, size_t n) {
	std::u16string r; r.reserve(n);
	for (size_t i = 0; i < n;) {
		unsigned char c = (unsigned char)s[i];
		if (c < 0x80) { r.push_back(c); ++i; }
		else if ((c >> 5) == 6 && i + 1 < n && ((unsigned char)s[i + 1] >> 6) == 2) { r.push_back((char16_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F))); i += 2; }
		else if ((c >> 4) == 14 && i + 2 < n && ((unsigned char)s[i + 1] >> 6) == 2 && ((unsigned char)s[i + 2] >> 6) == 2) { r.push_back((char16_t)(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F))); i += 3; }
		else { r.push_back(c); ++i; } // Latin-1 fallback
	}
	return r;
}

void loadTable(const char* name, int base) {
	char path[128];
	snprintf(path, sizeof path, "DATA\\LOCAL\\LNG\\ENG\\%s.tbl", name);
	size_t size = 0;
	uint8_t* data = (uint8_t*)ARCHIVE_AllocateBufferAndReadFile(nullptr, path, &size, __FILE__, __LINE__);
	if (!data || size < sizeof(D2TblHeaderStrc)) { free(data); return; }
	D2TblHeaderStrc h; memcpy(&h, data, sizeof h);
	Table t; t.base = base;
	t.strings.resize(h.nNumElements);
	const uint8_t* nodes = data + sizeof h + 2u * h.nNumElements;
	for (int i = 0; i < h.nHashTableSize; ++i) {
		D2TblNodeStrc n; memcpy(&n, nodes + (size_t)i * sizeof n, sizeof n);
		if (!n.bUsed || n.nIndexNo >= h.nNumElements || n.dwKeyOffset >= size || n.dwStringOffset >= size) continue;
		// The file stores the key offset before the string offset (D2MOO's struct names them the other way round).
		uint32_t keyOff = n.dwStringOffset, strOff = n.dwKeyOffset;
		const char* key = (const char*)data + keyOff;
		const char* str = (const char*)data + strOff;
		size_t len = n.nNameLen ? n.nNameLen - 1 : 0;
		if (strOff + len > size) len = size - strOff;
		t.strings[n.nIndexNo] = decode(str, strnlen(str, len));
		t.keys.emplace(std::string(key, strnlen(key, size - keyOff)), n.nIndexNo);
	}
	free(data);
	__android_log_print(ANDROID_LOG_INFO, "D2Native", "%s.tbl: %d strings", name, (int)t.strings.size());
	gTables.push_back(std::move(t));
}

void ensureLoaded() {
	if (gLoaded) return;
	gLoaded = true;
	loadTable("patchstring", 10000);
	loadTable("expansionstring", 20000);
	loadTable("string", 0);
}

const Unicode* asUnicode(const std::u16string& s) { return (const Unicode*)s.c_str(); }
}

short __stdcall D2LANG_GetTblIndex(char* szReference, const Unicode** pUnicode) {
	ensureLoaded();
	if (pUnicode) *pUnicode = asUnicode(gEmpty);
	if (!szReference) return 0;
	for (const Table& t : gTables) {
		auto it = t.keys.find(szReference);
		if (it != t.keys.end()) {
			if (pUnicode) *pUnicode = asUnicode(t.strings[it->second]);
			return (short)(t.base + it->second);
		}
	}
	return 0;
}

const Unicode* __fastcall D2LANG_GetStringFromTblIndex(short index) {
	ensureLoaded();
	int base = index >= 20000 ? 20000 : index >= 10000 ? 10000 : 0;
	for (const Table& t : gTables)
		if (t.base == base && index - base < (int)t.strings.size()) return asUnicode(t.strings[index - base]);
	return asUnicode(gEmpty);
}

const Unicode* __fastcall D2LANG_GetStringByReferenceString(char* string) {
	const Unicode* p = nullptr;
	D2LANG_GetTblIndex(string, &p);
	return p;
}
