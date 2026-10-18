// JNI bridge between pl.pd2.lab.d2.D2Native and the D2MOO core.
#include <Windows.h>
#include <D2BasicTypes.h>
#include <D2DataTbls.h>
#include <D2Dungeon.h>
#include <Drlg/D2DrlgDrlg.h>
#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>
#include <algorithm>

// storm_android.cpp asks this hook for every game file before trying the data directory.
typedef void* (*D2ReadHook)(const char* path, size_t* size);
extern "C" void d2native_set_read_hook(D2ReadHook hook);

static JNIEnv* gEnv;      // env of the thread inside the current native call
static jclass gClass;
static jmethodID gReadFile;
static bool gTablesLoaded;

static void* readThroughJava(const char* path, size_t* size) {
	if (!gEnv || !gReadFile) return nullptr;
	std::string p = path;
	for (char& c : p) { if (c == '\\') c = '/'; else if (c >= 'A' && c <= 'Z') c = (char)(c + 32); }
	jstring jpath = gEnv->NewStringUTF(p.c_str());
	jbyteArray bytes = (jbyteArray)gEnv->CallStaticObjectMethod(gClass, gReadFile, jpath);
	gEnv->DeleteLocalRef(jpath);
	if (gEnv->ExceptionCheck()) { gEnv->ExceptionClear(); return nullptr; }
	if (!bytes) return nullptr;
	jsize n = gEnv->GetArrayLength(bytes);
	// Callers parse text/binary in place and expect spare zero bytes after the data.
	void* buf = calloc(1, (size_t)n + 4);
	gEnv->GetByteArrayRegion(bytes, 0, n, (jbyte*)buf);
	gEnv->DeleteLocalRef(bytes);
	*size = (size_t)n;
	return buf;
}

struct EnvScope {
	explicit EnvScope(JNIEnv* env) { gEnv = env; }
	~EnvScope() { gEnv = nullptr; }
};

extern "C" JNIEXPORT jboolean JNICALL Java_pl_d2port_d2_D2Native_loadTables(JNIEnv* env, jclass cls) {
	EnvScope scope(env);
	if (gTablesLoaded) return JNI_TRUE;
	gClass = (jclass)env->NewGlobalRef(cls);
	gReadFile = env->GetStaticMethodID(cls, "readFile", "(Ljava/lang/String;)[B");
	d2native_set_read_hook(readThroughJava);
	// PD2's own compiled .bin tables, loaded exactly as the game does.
	DATATBLS_ToggleCompileTxtFlag(TRUE);
	DATATBLS_LoadAllTxts(nullptr, 0, 0);
	gTablesLoaded = sgptDataTables->pLevelsTxt != nullptr;
	__android_log_print(ANDROID_LOG_INFO, "D2Native", "tables loaded: %d", gTablesLoaded);
	return gTablesLoaded ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL Java_pl_d2port_d2_D2Native_allocAct(JNIEnv* env, jclass, jint act, jint seed, jint difficulty) {
	EnvScope scope(env);
	if (!gTablesLoaded) return 0;
	static const int towns[] = {1, 40, 75, 103, 109};
	if (act < 0 || act > 4) return 0;
	D2DrlgActStrc* pAct = DUNGEON_AllocAct((uint8_t)act, (uint32_t)seed, FALSE, nullptr, (uint8_t)difficulty, nullptr, towns[act], nullptr, nullptr);
	return (jlong)(intptr_t)pAct;
}

extern "C" JNIEXPORT void JNICALL Java_pl_d2port_d2_D2Native_freeAct(JNIEnv* env, jclass, jlong act) {
	EnvScope scope(env);
	if (act) DUNGEON_FreeAct((D2DrlgActStrc*)(intptr_t)act);
}

static D2DrlgLevelStrc* generatedLevel(jlong act, jint level) {
	D2DrlgActStrc* pAct = (D2DrlgActStrc*)(intptr_t)act;
	if (!pAct || level <= 0 || level >= sgptDataTables->nLevelsTxtRecordCount) return nullptr;
	if (DRLG_GetActNoFromLevelId(level) != pAct->nAct) return nullptr;
	D2DrlgLevelStrc* pLevel = DRLG_GetLevel(pAct->pDrlg, level);
	if (pLevel && !pLevel->pFirstRoomEx) DRLG_InitLevel(pLevel);
	return pLevel;
}

extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_levelInfo(JNIEnv* env, jclass, jlong act, jint level) {
	EnvScope scope(env);
	D2DrlgLevelStrc* pLevel = generatedLevel(act, level);
	if (!pLevel) return nullptr;
	jint v[7] = {pLevel->nDrlgType, pLevel->nLevelType, pLevel->nPosX, pLevel->nPosY, pLevel->nWidth, pLevel->nHeight, pLevel->nRooms};
	jintArray r = env->NewIntArray(7);
	env->SetIntArrayRegion(r, 0, 7, v);
	return r;
}

extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_levelRooms(JNIEnv* env, jclass, jlong act, jint level) {
	EnvScope scope(env);
	D2DrlgLevelStrc* pLevel = generatedLevel(act, level);
	if (!pLevel) return nullptr;
	std::vector<jint> v;
	for (D2DrlgRoomStrc* r = pLevel->pFirstRoomEx; r; r = r->pDrlgRoomNext) {
		v.push_back(r->nTileXPos); v.push_back(r->nTileYPos); v.push_back(r->nTileWidth); v.push_back(r->nTileHeight);
	}
	jintArray out = env->NewIntArray((jsize)v.size());
	env->SetIntArrayRegion(out, 0, (jsize)v.size(), v.data());
	return out;
}

// ------------------------------------------------------------------ rooms, tiles, collision

#include <Drlg/D2DrlgActivate.h>
#include <D2Collision.h>
#include <D2CMP.h>
#include <Archive.h>

const uint8_t* d2cmp_tile_raw(D2TileLibraryEntryStrc* p, size_t* fileSize, uint32_t* header);

static std::vector<D2TileLibraryEntryStrc*> gTileKeys; // image key -> tile entry chosen by D2
static uint8_t gPalette[768];
static bool gPaletteLoaded;

static int tileKey(D2TileLibraryEntryStrc* p) {
	for (size_t i = 0; i < gTileKeys.size(); ++i) if (gTileKeys[i] == p) return (int)i;
	gTileKeys.push_back(p);
	return (int)gTileKeys.size() - 1;
}

// Activates every room of the level (tiles + collision), as the game does when a player gets near.
static void activateLevel(D2DrlgLevelStrc* pLevel) {
	for (D2DrlgRoomStrc* r = pLevel->pFirstRoomEx; r; r = r->pDrlgRoomNext)
		if (!r->pRoom) DRLGACTIVATE_InitializeRoomEx(r);
}

// Draw list: per tile {layer, tileX, tileY, orientation, imageKey, flags}; layer 0 floor, 1 wall, 2 roof.
extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_levelTiles(JNIEnv* env, jclass, jlong act, jint level) {
	EnvScope scope(env);
	D2DrlgLevelStrc* pLevel = generatedLevel(act, level);
	if (!pLevel) return nullptr;
	activateLevel(pLevel);
	std::vector<jint> v;
	for (D2DrlgRoomStrc* r = pLevel->pFirstRoomEx; r; r = r->pDrlgRoomNext) {
		if (!r->pRoom) continue;
		for (int layer = 0; layer < 3; ++layer) {
			int n = 0;
			D2DrlgTileDataStrc* t = layer == 0 ? DUNGEON_GetFloorTilesFromRoom(r->pRoom, &n)
				: layer == 1 ? DUNGEON_GetWallTilesFromRoom(r->pRoom, &n) : DUNGEON_GetRoofTilesFromRoom(r->pRoom, &n);
			for (int i = 0; i < n; ++i) {
				if (!t[i].pTile) continue;
				v.push_back(layer); v.push_back(t[i].nPosX + r->nTileXPos); v.push_back(t[i].nPosY + r->nTileYPos);
				v.push_back(t[i].pTile->nType); v.push_back(tileKey(t[i].pTile)); v.push_back((jint)t[i].dwFlags);
			}
		}
	}
	jintArray out = env->NewIntArray((jsize)v.size());
	env->SetIntArrayRegion(out, 0, (jsize)v.size(), v.data());
	return out;
}

extern "C" JNIEXPORT jboolean JNICALL Java_pl_d2port_d2_D2Native_loadPalette(JNIEnv* env, jclass, jint act) {
	EnvScope scope(env);
	char path[64];
	snprintf(path, sizeof path, "DATA\\GLOBAL\\PALETTE\\ACT%d\\PAL.DAT", act + 1);
	size_t n = 0;
	void* data = ARCHIVE_AllocateBufferAndReadFile(nullptr, path, &n, __FILE__, __LINE__);
	gPaletteLoaded = data && n >= 768;
	if (gPaletteLoaded) memcpy(gPalette, data, 768); // stored as B, G, R
	free(data);
	return gPaletteLoaded ? JNI_TRUE : JNI_FALSE;
}

// Decodes a DT1 tile to ARGB: {width, height, left, top, pixels...}; left/top are relative to the
// tile's anchor (floor: top corner of the 160x80 diamond box; walls/shadows: bottom of that box;
// roofs: raised by the roof height).
extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_tileImage(JNIEnv* env, jclass, jint key) {
	if (key < 0 || key >= (int)gTileKeys.size()) return nullptr;
	D2TileLibraryEntryStrc* e = gTileKeys[key];
	size_t size = 0; uint32_t h = 0;
	const uint8_t* d = d2cmp_tile_raw(e, &size, &h);
	if (!d || h + 96 > size) return nullptr;
	auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, d + o, 4); return v; };
	auto i16 = [&](size_t o) { int16_t v; memcpy(&v, d + o, 2); return (int)v; };
	uint32_t blocks = u32(h + 0x48), count = u32(h + 0x50);
	if (count > 4096 || blocks + 20ull * count > size) return nullptr;
	struct Px { int x, y; uint8_t c; };
	std::vector<Px> px;
	static const int isoRow[15] = {4, 8, 12, 16, 20, 24, 28, 32, 28, 24, 20, 16, 12, 8, 4};
	for (uint32_t j = 0; j < count; ++j) {
		size_t b = blocks + 20ull * j;
		int bx = i16(b), by = i16(b + 2), format = (int)(u32(b + 8) & 0xFFFF);
		uint32_t len = u32(b + 10), off = blocks + u32(b + 16);
		if ((size_t)off + len > size) continue;
		const uint8_t* p = d + off; const uint8_t* end = p + len;
		if (format == 1) { // isometric diamond, 32x15
			for (int y = 0; y < 15 && p < end; ++y)
				for (int x = (32 - isoRow[y]) / 2, k = 0; k < isoRow[y] && p < end; ++k, ++x) px.push_back({bx + x, by + y, *p++});
		} else { // run-length: (skip, count) pairs, (0, 0) ends a row
			int x = 0, y = 0;
			while (p + 2 <= end) {
				int skip = p[0], n = p[1]; p += 2;
				if (!skip && !n) { x = 0; ++y; continue; }
				x += skip;
				for (int k = 0; k < n && p < end; ++k) px.push_back({bx + x++, by + y, *p++});
			}
		}
	}
	if (px.empty()) return nullptr;
	int l = INT32_MAX, t = INT32_MAX, r = INT32_MIN, btm = INT32_MIN;
	for (const Px& q : px) { l = std::min(l, q.x); t = std::min(t, q.y); r = std::max(r, q.x + 1); btm = std::max(btm, q.y + 1); }
	int w = r - l, hgt = btm - t;
	if (w <= 0 || hgt <= 0 || w > 2048 || hgt > 2048) return nullptr;
	int anchorY = e->nType == 0 ? 0 : e->nType == 15 ? -e->nRoofHeight : 80;
	std::vector<jint> out(4 + (size_t)w * hgt, 0);
	out[0] = w; out[1] = hgt; out[2] = l; out[3] = t + anchorY;
	for (const Px& q : px) {
		if (!q.c) continue;
		const uint8_t* c = gPalette + q.c * 3;
		uint32_t alpha = e->nType == 13 ? 0x80u : 0xFFu; // shadows are translucent
		out[4 + (size_t)(q.y - t) * w + (q.x - l)] = (jint)(alpha << 24 | c[2] << 16 | c[1] << 8 | c[0]);
	}
	jintArray arr = env->NewIntArray((jsize)out.size());
	env->SetIntArrayRegion(arr, 0, (jsize)out.size(), out.data());
	return arr;
}

// Collision masks of a level, one short per subtile, rows of width*5 (D2 collision flags).
extern "C" JNIEXPORT jshortArray JNICALL Java_pl_d2port_d2_D2Native_levelCollision(JNIEnv* env, jclass, jlong act, jint level) {
	EnvScope scope(env);
	D2DrlgLevelStrc* pLevel = generatedLevel(act, level);
	if (!pLevel) return nullptr;
	activateLevel(pLevel);
	int w = pLevel->nWidth * 5, h = pLevel->nHeight * 5, x0 = pLevel->nPosX * 5, y0 = pLevel->nPosY * 5;
	std::vector<jshort> m((size_t)w * h, (jshort)0xFFFF);
	for (D2DrlgRoomStrc* r = pLevel->pFirstRoomEx; r; r = r->pDrlgRoomNext) {
		if (!r->pRoom || !r->pRoom->pCollisionGrid) continue;
		D2RoomCollisionGridStrc* g = r->pRoom->pCollisionGrid;
		for (int y = 0; y < g->pRoomCoords.nSubtileHeight; ++y)
			for (int x = 0; x < g->pRoomCoords.nSubtileWidth; ++x) {
				int ix = g->pRoomCoords.nSubtileX + x - x0, iy = g->pRoomCoords.nSubtileY + y - y0;
				if (ix >= 0 && iy >= 0 && ix < w && iy < h) m[(size_t)iy * w + ix] = (jshort)g->pCollisionMask[y * g->pRoomCoords.nSubtileWidth + x];
			}
	}
	jshortArray out = env->NewShortArray((jsize)m.size());
	env->SetShortArrayRegion(out, 0, (jsize)m.size(), m.data());
	return out;
}
