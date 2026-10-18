// Android implementation of the D2CMP tile library functions D2Common needs (D2MOO ships stubs).
// A DT1 file is loaded once and shared by every slot array that asks for it. Tile entries copy
// the first 0x28 bytes of the DT1 tile header, which match D2TileLibraryEntryStrc, and keep the
// 25 sub-tile collision flags next to them.
//
// Unverified against the original D2Cmp.dll: the order in which GetTiles returns matching tiles
// (slots in order, newest tile of a library first, like the pPrev chains in D2MOO's hash structs).
// The order only changes which variant a rarity roll picks, not how many seeds are rolled.
#include <Windows.h>
#include <D2BasicTypes.h>
#include <D2CMP.h>
#include <Archive.h>
#include <android/log.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct Library;
struct TileEntry {
	D2TileLibraryEntryStrc e;
	uint8_t subFlags[25];
	Library* lib;       // owner, keeps the raw DT1 bytes for drawing
	uint32_t header;    // offset of the 96-byte tile header in the DT1
};
struct Library {
	std::string name;
	std::vector<TileEntry> tiles;
	std::vector<uint8_t> raw;
};
std::unordered_map<std::string, Library*> gLibraries;

std::string normalize(const char* szFileName) {
	std::string p = szFileName;
	for (char& c : p) { if (c == '/') c = '\\'; else if (c >= 'A' && c <= 'Z') c = (char)(c + 32); }
	if (p.compare(0, 5, "data\\") != 0) p = "data\\global\\tiles\\" + p;
	return p;
}

Library* loadLibrary(const std::string& path) {
	auto it = gLibraries.find(path);
	if (it != gLibraries.end()) return it->second;
	size_t size = 0;
	uint8_t* data = (uint8_t*)ARCHIVE_AllocateBufferAndReadFile(nullptr, path.c_str(), &size, __FILE__, __LINE__);
	Library* lib = new Library();
	lib->name = path;
	if (data && size >= 276 + 8) {
		int32_t nTiles = 0, tileStart = 0;
		memcpy(&nTiles, data + 268, 4);
		memcpy(&tileStart, data + 272, 4);
		for (int i = 0; i < nTiles; ++i) {
			size_t off = (size_t)tileStart + (size_t)i * 96;
			if (off + 96 > size) break;
			TileEntry t = {};
			t.lib = lib;
			t.header = (uint32_t)off;
			memcpy(&t.e, data + off, 0x28);
			memcpy(t.subFlags, data + off + 0x28, 25);
			memcpy(t.e.dwTileFlags, t.subFlags, 4);
			memcpy(&t.e.dwBlockOffset_pBlock, data + off + 0x48, 4);
			memcpy(&t.e.nBlockSize, data + off + 0x4C, 4);
			memcpy(&t.e.nBlocks, data + off + 0x50, 4);
			lib->tiles.push_back(t);
		}
	}
	if (data) lib->raw.assign(data, data + size);
	free(data);
	gLibraries[path] = lib;
	return lib;
}

TileEntry* entryOf(D2TileLibraryEntryStrc* p) { return (TileEntry*)p; }
}

void __stdcall D2CMP_10087_LoadTileLibrarySlot(D2TileLibraryHashStrc** ppTileLibraryHash, const char* szFileName) {
	if (!ppTileLibraryHash || !szFileName || !*szFileName) return;
	Library* lib = loadLibrary(normalize(szFileName));
	for (int i = 0; i < 32; ++i) {
		if (ppTileLibraryHash[i] == (D2TileLibraryHashStrc*)lib) return;
		if (!ppTileLibraryHash[i]) { ppTileLibraryHash[i] = (D2TileLibraryHashStrc*)lib; return; }
	}
	__android_log_print(ANDROID_LOG_WARN, "D2Native", "tile slots full, dropped %s", szFileName);
}

int __stdcall D2CMP_10088_GetTiles(D2TileLibraryHashStrc** ppTileLibraryHash, int nType, int nStyle, int nSequence, D2TileLibraryEntryStrc** pTileList, int nTileListSize) {
	int n = 0;
	if (!ppTileLibraryHash) return 0;
	for (int i = 0; i < 32 && ppTileLibraryHash[i]; ++i) {
		Library* lib = (Library*)ppTileLibraryHash[i];
		for (int k = (int)lib->tiles.size() - 1; k >= 0; --k) {
			D2TileLibraryEntryStrc& e = lib->tiles[k].e;
			if (e.nType == nType && e.nStyle == nStyle && e.nSequence == nSequence) {
				if (n < nTileListSize) pTileList[n] = &e;
				++n;
			}
		}
	}
	return n < nTileListSize ? n : nTileListSize;
}

int __stdcall D2CMP_10077_GetTileType(D2TileLibraryEntryStrc* p) { return p ? p->nType : 0; }
int __stdcall D2CMP_10078_GetTileStyle(D2TileLibraryEntryStrc* p) { return p ? p->nStyle : 0; }
int __stdcall D2CMP_10079_GetTileFlags(D2TileLibraryEntryStrc* p) { return p ? p->nFlags : 0; }
int __stdcall D2CMP_10081_GetTileRarity(D2TileLibraryEntryStrc* p) { return p ? p->nRarity_Frame : 0; }
int __stdcall D2CMP_10082_GetTileSequence(D2TileLibraryEntryStrc* p) { return p ? p->nSequence : 0; }
uint8_t* __stdcall D2CMP_10085_GetTileFlagArray(D2TileLibraryEntryStrc* p) { return p ? entryOf(p)->subFlags : nullptr; }

// Colour remapping is only needed for drawing; the game logic gets an identity table.
uint8_t* __stdcall D2CMP_MixPalette(uint8_t, int) {
	static uint8_t identity[256];
	static bool ready = false;
	if (!ready) { for (int i = 0; i < 256; ++i) identity[i] = (uint8_t)i; ready = true; }
	return identity;
}

// Raw DT1 bytes of a tile for drawing: the whole file and the offset of the tile's 96-byte header.
const uint8_t* d2cmp_tile_raw(D2TileLibraryEntryStrc* p, size_t* fileSize, uint32_t* header) {
	if (!p) return nullptr;
	TileEntry* t = entryOf(p);
	*fileSize = t->lib->raw.size();
	*header = t->header;
	return t->lib->raw.data();
}
