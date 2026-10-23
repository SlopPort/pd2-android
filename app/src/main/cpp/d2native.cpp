// JNI bridge between pl.pd2.lab.d2.D2Native and the D2MOO core.
#include <Windows.h>
#include <D2BasicTypes.h>
#include <D2DataTbls.h>
#include <DataTbls/LevelsTbls.h>
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

// ------------------------------------------------------------------ game (player, 25 fps frames)

#include "game.h"

extern "C" JNIEXPORT jboolean JNICALL Java_pl_d2port_d2_D2Native_spawnPlayer(JNIEnv* env, jclass, jlong act, jint charClass) {
	EnvScope scope(env);
	return d2game_spawn((D2DrlgActStrc*)(intptr_t)act, charClass) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_pl_d2port_d2_D2Native_setRun(JNIEnv*, jclass, jboolean run) { d2game_set_run(run); }

extern "C" JNIEXPORT jboolean JNICALL Java_pl_d2port_d2_D2Native_moveTo(JNIEnv* env, jclass, jint x, jint y) {
	EnvScope scope(env);
	return d2game_move_to(x, y) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_pl_d2port_d2_D2Native_tick(JNIEnv* env, jclass) {
	EnvScope scope(env);
	d2game_tick();
}

extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_playerState(JNIEnv* env, jclass) {
	jint s[9];
	d2game_player_state((int*)s);
	jintArray out = env->NewIntArray(9);
	env->SetIntArrayRegion(out, 0, 9, s);
	return out;
}

// ------------------------------------------------------------------ unit graphics (COF + DCC)

#include "dcc.h"
#include <map>

namespace {
const char* kComponents[16] = {"HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8"};
std::map<std::string, std::vector<uint8_t>> gFileCache;

const std::vector<uint8_t>* gameFile(const std::string& path) {
	auto it = gFileCache.find(path);
	if (it != gFileCache.end()) return it->second.empty() ? nullptr : &it->second;
	size_t n = 0;
	void* data = ARCHIVE_AllocateBufferAndReadFile(nullptr, path.c_str(), &n, __FILE__, __LINE__);
	std::vector<uint8_t>& v = gFileCache[path];
	if (data) v.assign((uint8_t*)data, (uint8_t*)data + n);
	free(data);
	return v.empty() ? nullptr : &v;
}
}

// Composite animation of a character/monster token for one COF direction:
// {frames, animRate, cofDirs, boxX, boxY, width, height, argb frames...}; box relative to the foot point.
// armor: 16 armour/weapon codes per component ("LIT", "HAX", ...), empty for none.
static jintArray compositeAnimation(JNIEnv* env, const std::string& category, const std::string& token, const std::string& mode, const std::string& wclass, const std::string* armor, int dir);

extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_unitAnimation(JNIEnv* env, jclass, jstring jtoken, jstring jmode, jstring jwclass, jobjectArray jarmor, jint dir) {
	EnvScope scope(env);
	const char* tk = env->GetStringUTFChars(jtoken, nullptr); std::string token = tk; env->ReleaseStringUTFChars(jtoken, tk);
	const char* md = env->GetStringUTFChars(jmode, nullptr); std::string mode = md; env->ReleaseStringUTFChars(jmode, md);
	const char* wc = env->GetStringUTFChars(jwclass, nullptr); std::string wclass = wc; env->ReleaseStringUTFChars(jwclass, wc);
	std::string armor[16];
	for (int i = 0; i < 16 && i < env->GetArrayLength(jarmor); ++i) {
		jstring s = (jstring)env->GetObjectArrayElement(jarmor, i);
		if (s) { const char* c = env->GetStringUTFChars(s, nullptr); armor[i] = c; env->ReleaseStringUTFChars(s, c); }
	}
	return compositeAnimation(env, "CHARS", token, mode, wclass, armor, dir);
}

// category: CHARS, MONSTERS or OBJECTS (folder under DATA\GLOBAL).
static jintArray compositeAnimation(JNIEnv* env, const std::string& category, const std::string& token, const std::string& mode, const std::string& wclass, const std::string* armor, int dir) {
	std::string base = "DATA\\GLOBAL\\" + category + "\\" + token + "\\";
	const std::vector<uint8_t>* cofData = gameFile(base + "COF\\" + token + mode + wclass + ".COF");
	Cof cof;
	if (!cofData || !cof_parse(cofData->data(), cofData->size(), cof) || dir < 0 || dir >= cof.dirs) return nullptr;

	// Decode every layer of this direction.
	std::vector<DccDirection> layerDir(16);
	std::vector<bool> present(16, false);
	int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
	for (const CofLayer& l : cof.layer) {
		if (l.component >= 16 || armor[l.component].empty()) continue;
		std::string comp = kComponents[l.component];
		std::string path = base + comp + "\\" + token + comp + armor[l.component] + mode + l.weaponClass + ".DCC";
		const std::vector<uint8_t>* dcc = gameFile(path);
		if (!dcc || !dcc_decode_direction(dcc->data(), dcc->size(), dir, layerDir[l.component])) continue;
		present[l.component] = true;
		const DccDirection& d = layerDir[l.component];
		x0 = std::min(x0, d.boxX); y0 = std::min(y0, d.boxY); x1 = std::max(x1, d.boxX + d.width); y1 = std::max(y1, d.boxY + d.height);
	}
	if (x0 >= x1 || y0 >= y1) return nullptr;
	int w = x1 - x0, h = y1 - y0;
	std::vector<jint> out(7 + (size_t)w * h * cof.frames, 0);
	out[0] = cof.frames; out[1] = cof.animRate; out[2] = cof.dirs; out[3] = x0; out[4] = y0; out[5] = w; out[6] = h;
	for (int f = 0; f < cof.frames; ++f) {
		jint* img = &out[7 + (size_t)w * h * f];
		for (int k = 0; k < cof.layers; ++k) {
			int comp = cof.priority[((size_t)dir * cof.frames + f) * cof.layers + k];
			if (comp >= 16 || !present[comp]) continue;
			const DccDirection& d = layerDir[comp];
			if (f >= (int)d.frames.size()) continue;
			const std::vector<uint8_t>& px = d.frames[f];
			bool translucent = false;
			for (const CofLayer& l : cof.layer) if (l.component == comp && l.transparent) translucent = true;
			for (int y = 0; y < d.height; ++y)
				for (int x = 0; x < d.width; ++x) {
					uint8_t c = px[(size_t)y * d.width + x];
					if (!c) continue;
					const uint8_t* rgb = gPalette + c * 3;
					uint32_t a = translucent ? 0x80u : 0xFFu;
					img[(size_t)(y + d.boxY - y0) * w + (x + d.boxX - x0)] = (jint)(a << 24 | rgb[2] << 16 | rgb[1] << 8 | rgb[0]);
				}
		}
	}
	jintArray arr = env->NewIntArray((jsize)out.size());
	env->SetIntArrayRegion(arr, 0, (jsize)out.size(), out.data());
	return arr;
}

// ------------------------------------------------------------------ D2Game (server) game

#include "sgame.h"

extern "C" JNIEXPORT void JNICALL Java_pl_d2port_d2_D2Native_setGameSeed(JNIEnv* env, jclass, jint seed) { EnvScope scope(env); sgame_set_seed(seed); }

extern "C" JNIEXPORT jlong JNICALL Java_pl_d2port_d2_D2Native_startGame(JNIEnv* env, jclass, jint charClass, jint difficulty) {
	EnvScope scope(env);
	if (!gTablesLoaded || !sgame_create("Android", charClass, difficulty)) return 0;
	return (jlong)(intptr_t)sgame_act();
}

extern "C" JNIEXPORT void JNICALL Java_pl_d2port_d2_D2Native_serverTick(JNIEnv* env, jclass) { EnvScope scope(env); sgame_tick(); }
extern "C" JNIEXPORT jboolean JNICALL Java_pl_d2port_d2_D2Native_serverReady(JNIEnv* env, jclass) { EnvScope scope(env); return sgame_ready() ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT void JNICALL Java_pl_d2port_d2_D2Native_serverMove(JNIEnv* env, jclass, jint x, jint y, jboolean run) { EnvScope scope(env); sgame_move(x, y, run); }
extern "C" JNIEXPORT void JNICALL Java_pl_d2port_d2_D2Native_serverAttack(JNIEnv* env, jclass, jint type, jint guid) { EnvScope scope(env); sgame_attack(type, guid); }
extern "C" JNIEXPORT jlong JNICALL Java_pl_d2port_d2_D2Native_serverAct(JNIEnv* env, jclass) { EnvScope scope(env); return (jlong)(intptr_t)sgame_act(); }

extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_serverPlayer(JNIEnv* env, jclass) {
	EnvScope scope(env);
	jint s[9];
	sgame_player_state((int*)s);
	jintArray out = env->NewIntArray(9);
	env->SetIntArrayRegion(out, 0, 9, s);
	return out;
}

extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_serverUnits(JNIEnv* env, jclass) {
	EnvScope scope(env);
	std::vector<int> v;
	sgame_units(v);
	jintArray out = env->NewIntArray((jsize)v.size());
	env->SetIntArrayRegion(out, 0, (jsize)v.size(), (const jint*)v.data());
	return out;
}

// "CATEGORY|TOKEN|MODE|WCLASS|c0,c1,...,c15" or null.
extern "C" JNIEXPORT jstring JNICALL Java_pl_d2port_d2_D2Native_groundItemFile(JNIEnv* env, jclass, jint guid) {
	EnvScope scope(env);
	std::string f = sgame_ground_item_file(guid);
	return f.empty() ? nullptr : env->NewStringUTF(f.c_str());
}

extern "C" JNIEXPORT jstring JNICALL Java_pl_d2port_d2_D2Native_unitLook(JNIEnv* env, jclass, jint type, jint guid) {
	EnvScope scope(env);
	std::string category, token, mode, wclass, armor[16];
	if (!sgame_unit_look(type, guid, category, token, mode, wclass, armor)) return nullptr;
	std::string key = category + "|" + token + "|" + mode + "|" + wclass + "|";
	for (int i = 0; i < 16; ++i) { if (i) key += ","; key += armor[i]; }
	return env->NewStringUTF(key.c_str());
}

extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_lookAnimation(JNIEnv* env, jclass, jstring jkey, jint dir) {
	EnvScope scope(env);
	const char* k = env->GetStringUTFChars(jkey, nullptr); std::string key = k; env->ReleaseStringUTFChars(jkey, k);
	std::vector<std::string> f;
	size_t start = 0;
	for (size_t i = 0; i <= key.size(); ++i) if (i == key.size() || key[i] == '|') { f.push_back(key.substr(start, i - start)); start = i + 1; }
	if (f.size() != 5) return nullptr;
	std::string armor[16];
	int c = 0; start = 0;
	for (size_t i = 0; i <= f[4].size() && c < 16; ++i) if (i == f[4].size() || f[4][i] == ',') { armor[c++] = f[4].substr(start, i - start); start = i + 1; }
	return compositeAnimation(env, f[0], f[1], f[2], f[3], armor, dir);
}

// Tiles of every room the game keeps active (around the players), whatever its level: this is what
// D2Client draws, so crossing into the next level is seamless. Same layout as levelTiles.
extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_activeTiles(JNIEnv* env, jclass, jlong act) {
	EnvScope scope(env);
	D2DrlgActStrc* pAct = (D2DrlgActStrc*)(intptr_t)act;
	if (!pAct || !pAct->pDrlg) return nullptr;
	std::vector<jint> v;
	for (D2DrlgLevelStrc* pLevel = pAct->pDrlg->pLevel; pLevel; pLevel = pLevel->pNextLevel) {
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
	}
	jintArray out = env->NewIntArray((jsize)v.size());
	env->SetIntArrayRegion(out, 0, (jsize)v.size(), v.data());
	return out;
}

// ------------------------------------------------------------------ automap


namespace {
// Automap cells of rooms the player has been near (D2 reveals the automap the same way), kept after
// the game unloads the room: {tileX, tileY, cellId} per tile.
std::map<D2DrlgRoomStrc*, std::vector<jint>> gRevealed;
}

// Automap of revealed rooms: {tileX, tileY, cellId} per tile; the cell is the frame in
// data/global/ui/automap/maximap.dc6, chosen by D2Common from automap.txt (level type, tile type,
// style, sequence) as D2Client does.
extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_automapCells(JNIEnv* env, jclass, jlong act) {
	EnvScope scope(env);
	D2DrlgActStrc* pAct = (D2DrlgActStrc*)(intptr_t)act;
	if (!pAct || !pAct->pDrlg) return nullptr;
	for (D2DrlgLevelStrc* pLevel = pAct->pDrlg->pLevel; pLevel; pLevel = pLevel->pNextLevel) {
		for (D2DrlgRoomStrc* r = pLevel->pFirstRoomEx; r; r = r->pDrlgRoomNext) {
			if (!r->pRoom || gRevealed.count(r)) continue;
			std::vector<jint>& cells = gRevealed[r];
			for (int layer = 0; layer < 2; ++layer) {
				int n = 0;
				D2DrlgTileDataStrc* t = layer == 0 ? DUNGEON_GetFloorTilesFromRoom(r->pRoom, &n) : DUNGEON_GetWallTilesFromRoom(r->pRoom, &n);
				for (int i = 0; i < n; ++i) {
					D2TileLibraryEntryStrc* e = t[i].pTile;
					if (!e || (t[i].dwFlags & MAPTILE_HIDDEN)) continue;
					int cell = DATATBLS_GetAutomapCellId(pLevel->nLevelType, e->nType, e->nStyle, e->nSequence);
					if (cell < 0) continue;
					cells.push_back(t[i].nPosX + r->nTileXPos); cells.push_back(t[i].nPosY + r->nTileYPos); cells.push_back(cell);
				}
			}
		}
	}
	std::vector<jint> v;
	for (auto& kv : gRevealed) v.insert(v.end(), kv.second.begin(), kv.second.end());
	jintArray out = env->NewIntArray((jsize)v.size());
	env->SetIntArrayRegion(out, 0, (jsize)v.size(), v.data());
	return out;
}

// All frames of a DC6 file in the act palette: {count, then per frame w, h, offX, offY, argb...}.
extern "C" JNIEXPORT jintArray JNICALL Java_pl_d2port_d2_D2Native_dc6Frames(JNIEnv* env, jclass, jstring jpath) {
	EnvScope scope(env);
	const char* p = env->GetStringUTFChars(jpath, nullptr); std::string path = p; env->ReleaseStringUTFChars(jpath, p);
	const std::vector<uint8_t>* data = gameFile(path);
	std::vector<Dc6Frame> frames;
	if (!data || !dc6_decode(data->data(), data->size(), frames)) return nullptr;
	std::vector<jint> v;
	v.push_back((jint)frames.size());
	for (const Dc6Frame& f : frames) {
		v.push_back(f.width); v.push_back(f.height); v.push_back(f.offsetX); v.push_back(f.offsetY);
		for (uint8_t c : f.pixels) {
			if (!c) { v.push_back(0); continue; }
			const uint8_t* rgb = gPalette + c * 3;
			v.push_back((jint)(0xFFu << 24 | rgb[2] << 16 | rgb[1] << 8 | rgb[0]));
		}
	}
	jintArray out = env->NewIntArray((jsize)v.size());
	env->SetIntArrayRegion(out, 0, (jsize)v.size(), v.data());
	return out;
}

// ------------------------------------------------------------------ developer: walk towards a level

#include "route.h"

// One step of the developer "walk to level" helper: plans over the loaded rooms towards the given
// level and sends a walk/run command about 12 subtiles along the route, as a player's click would.
// Returns false when the hero is already in that level or no route exists.
extern "C" JNIEXPORT jboolean JNICALL Java_pl_d2port_d2_D2Native_devWalkToLevel(JNIEnv* env, jclass, jint level, jboolean run) {
	EnvScope scope(env);
	D2DrlgActStrc* pAct = sgame_act();
	if (!pAct || !sgame_ready()) return JNI_FALSE;
	int s[9];
	sgame_player_state(s);
	if (s[6] == level) return JNI_FALSE;
	D2DrlgLevelStrc* pLevel = DRLG_GetLevel(pAct->pDrlg, level);
	if (!pLevel) return JNI_FALSE;
	int tx = (pLevel->nPosX + pLevel->nWidth / 2) * 5, ty = (pLevel->nPosY + pLevel->nHeight / 2) * 5;
	// 250 subtiles covers the whole Rogue camp, so the goal level is found by search, not by the
	// closest point to its centre (which can be a dead end along the palisade).
	std::vector<RoutePoint> route = test_plan_route(pAct, s[0], s[1], tx, ty, 250, level);
	if (route.empty()) return JNI_FALSE;
	const RoutePoint& p = route[std::min<size_t>(route.size() - 1, 12)];
	sgame_move(p.x, p.y, run);
	return JNI_TRUE;
}

// Developer helper: one walk/run command along a planned route towards a subtile.
extern "C" JNIEXPORT jboolean JNICALL Java_pl_d2port_d2_D2Native_devWalkToPoint(JNIEnv* env, jclass, jint x, jint y, jboolean run) {
	EnvScope scope(env);
	D2DrlgActStrc* pAct = sgame_act();
	if (!pAct || !sgame_ready()) return JNI_FALSE;
	int s[9];
	sgame_player_state(s);
	if (std::abs(s[0] - x) + std::abs(s[1] - y) <= 4) return JNI_FALSE;
	std::vector<RoutePoint> route = test_plan_route(pAct, s[0], s[1], x, y, 160);
	if (route.empty()) return JNI_FALSE;
	const RoutePoint& p = route[std::min<size_t>(route.size() - 1, 12)];
	sgame_move(p.x, p.y, run);
	return JNI_TRUE;
}
