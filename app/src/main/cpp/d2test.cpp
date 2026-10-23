// Command-line check of the native D2 core, run on the phone:
//   d2test <data root> <seed> [reference bin dir]
// Loads the PD2 tables with D2MOO, optionally compares the compiled .bin records with the
// original PD2 ones, then builds Act I with the original DRLG and prints every level.
#include <Windows.h>
#include <D2BasicTypes.h>
#include <D2DataTbls.h>
#include <D2Dungeon.h>
#include <Drlg/D2DrlgDrlg.h>
#include <Drlg/D2DrlgActivate.h>
#include <D2Collision.h>
#include <Path/Path.h>
#include <Path/PathMisc.h>
#include <Units/Units.h>
#include "game.h"
#include "sgame.h"
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <vector>
extern "C" void d2native_set_data_root(const char* root);

static std::vector<unsigned char> readAll(const char* path) {
	std::vector<unsigned char> v;
	FILE* f = fopen(path, "rb");
	if (!f) return v;
	int c;
	while ((c = fgetc(f)) != EOF) v.push_back((unsigned char)c);
	fclose(f);
	return v;
}

static void compareBins(const char* root, const char* refDir) {
	const char* names[] = {"itemtypes", "levels", "leveldefs", "lvlprest", "lvltypes", "lvlmaze", "lvlsub", "lvlwarp",
		"monstats", "monstats2", "objects", "objgroup", "monlvl", "monprop", "superuniques", "monumod", "skills",
		"missiles", "states", "charstats", "weapons", "armor", "misc", "automap", "hireling", "experience"};
	for (const char* n : names) {
		char a[512], b[512];
		snprintf(a, sizeof a, "%s/data/global/excel/%s.bin", root, n);
		snprintf(b, sizeof b, "%s/%s.bin", refDir, n);
		std::vector<unsigned char> va = readAll(a), vb = readAll(b);
		if (va.size() < 4 || vb.size() < 4) { printf("cmp %-12s missing %s\n", n, va.size() < 4 ? "ours" : "ref"); continue; }
		int na = *(int*)va.data(), nb = *(int*)vb.data();
		size_t ra = na ? (va.size() - 4) / na : 0, rb = nb ? (vb.size() - 4) / nb : 0;
		size_t diff = 0, first = (size_t)-1;
		if (ra == rb)
			for (size_t i = 4; i < va.size() && i < vb.size(); ++i)
				if (va[i] != vb[i]) { if (first == (size_t)-1) first = i - 4; ++diff; }
		printf("cmp %-12s rows %d/%d recsize %zu/%zu diffbytes %zu", n, na, nb, ra, rb, diff);
		if (first != (size_t)-1 && ra) printf(" first row %zu off 0x%zx", first / ra, first % ra);
		printf("\n");
	}
}

int main(int argc, char** argv) {
	const char* root = argc > 1 ? argv[1] : "/data/local/tmp/d2data";
	d2native_set_data_root(root);
	uint32_t seed = argc > 2 ? strtoul(argv[2], 0, 10) : 12345;
	// Default: load PD2's original compiled .bin tables, exactly as the game does.
	// D2TXT=1 compiles the txt tables instead (diagnostic path for our Fog txt parser).
	const char* txt = getenv("D2TXT");
	DATATBLS_ToggleCompileTxtFlag(!(txt && *txt == '1'));
	printf("loading tables\n"); fflush(stdout);
	DATATBLS_LoadAllTxts(nullptr, 0, 0);
	printf("tables loaded\n"); fflush(stdout);
	if (argc > 3) { compareBins(root, argv[3]); return 0; }
	if (getenv("D2SERVER")) {
		// A hang becomes an abort with a stack in logcat.
		signal(SIGALRM, [](int) { abort(); });
		alarm(25);
		// Full D2Game: new single-player game, the player walks, server frames run.
		if (!sgame_create("Tester", 4, 0)) { printf("game not created\n"); return 1; }
		int s[9];
		sgame_player_state(s);
		printf("player at %d,%d level %d mode %d\n", s[0], s[1], s[6], s[4]);
		std::vector<int> units;
		printf("units: %d\n", sgame_units(units));
		for (size_t i = 0; i + 7 < units.size() && i < 8 * 40; i += 8)
			printf("  type %d class %d at %d,%d mode %d\n", units[i], units[i + 1], units[i + 3], units[i + 4], units[i + 5]);
		int waited = 0;
		while (!sgame_ready() && waited < 100) { usleep(40000); sgame_tick(); ++waited; }
		printf("in game after %d frames\n", waited);
		sgame_move(s[0] + 15, s[1], false);
		for (int f = 1; f <= 60; ++f) {
			usleep(40000);
			sgame_tick();
			sgame_player_state(s);
			if (f % 10 == 0) printf("frame %d: %d,%d mode %d\n", f, s[0], s[1], s[4]);
		}
		printf("units after: %d\n", sgame_units(units));
		for (size_t i = 0; i + 7 < units.size(); i += 8)
			if (units[i] != 4) printf("  type %d class %d at %d,%d mode %d\n", units[i], units[i + 1], units[i + 3], units[i + 4], units[i + 5]);
		return 0;
	}
	printf("lvlmaze %p count %d, lvlprest %p count %d\n", (void*)sgptDataTables->pLvlMazeTxt, sgptDataTables->nLvlMazeTxtRecordCount, (void*)sgptDataTables->pLvlPrestTxt, sgptDataTables->nLvlPrestTxtRecordCount);

	for (int nLevel = 1; nLevel <= 39; ++nLevel) {
		D2LevelDefBin* pDef = DATATBLS_GetLevelDefRecord(nLevel);
		printf("def %d: type=%d drlg=%d\n", nLevel, pDef ? (int)pDef->dwLevelType : -1, pDef ? (int)pDef->dwDrlgType : -1);
	}
	fflush(stdout);
	D2DrlgActStrc* pAct = DUNGEON_AllocAct(0, seed, FALSE, nullptr, 0, nullptr, 1, nullptr, nullptr);
	printf("act allocated %p\n", (void*)pAct); fflush(stdout);
	for (int nLevel = 1; nLevel <= 39; ++nLevel) {
		D2DrlgLevelStrc* pLevel = DRLG_GetLevel(pAct->pDrlg, nLevel);
		if (!pLevel) { printf("level %d: none\n", nLevel); continue; }
		DRLG_InitLevel(pLevel);
		printf("level %d type=%d drlg=%d pos=%d,%d size=%dx%d rooms=%d\n", nLevel, pLevel->nLevelType, pLevel->nDrlgType,
			pLevel->nPosX, pLevel->nPosY, pLevel->nWidth, pLevel->nHeight, pLevel->nRooms);
		fflush(stdout);
	}
	// Collision of one level: activate every room (as the game does when it streams rooms in)
	// and write the subtile grid as a PGM image: white = walkable, black = blocks walking.
	int nDump = getenv("D2LEVEL") ? atoi(getenv("D2LEVEL")) : 1;
	if (D2DrlgLevelStrc* pLevel = DRLG_GetLevel(pAct->pDrlg, nDump)) {
		int w = pLevel->nWidth * 5, h = pLevel->nHeight * 5, x0 = pLevel->nPosX * 5, y0 = pLevel->nPosY * 5;
		std::vector<unsigned char> img((size_t)w * h, 128);
		int nRooms = 0, nGrids = 0;
		for (D2DrlgRoomStrc* r = pLevel->pFirstRoomEx; r; r = r->pDrlgRoomNext) {
			DRLGACTIVATE_InitializeRoomEx(r);
			++nRooms;
			if (!r->pRoom || !r->pRoom->pCollisionGrid) continue;
			++nGrids;
			D2RoomCollisionGridStrc* g = r->pRoom->pCollisionGrid;
			for (int y = 0; y < g->pRoomCoords.nSubtileHeight; ++y)
				for (int x = 0; x < g->pRoomCoords.nSubtileWidth; ++x) {
					int ix = g->pRoomCoords.nSubtileX + x - x0, iy = g->pRoomCoords.nSubtileY + y - y0;
					if (ix < 0 || iy < 0 || ix >= w || iy >= h) continue;
					uint16_t m = g->pCollisionMask[y * g->pRoomCoords.nSubtileWidth + x];
					img[(size_t)iy * w + ix] = (m & 1) ? 0 : (m ? 96 : 255); // wall, other flags, free
				}
		}
		// Second image: tiles (floor green, walls blue, other grey) with collision walls in red.
		std::vector<unsigned char> rgb((size_t)w * h * 3, 0);
		int nFloors = 0, nWalls = 0, nNoGfx = 0;
		for (D2DrlgRoomStrc* r = pLevel->pFirstRoomEx; r; r = r->pDrlgRoomNext) {
			if (!r->pRoom) continue;
			for (int pass = 0; pass < 2; ++pass) {
				int n = 0;
				D2DrlgTileDataStrc* t = pass == 0 ? DUNGEON_GetFloorTilesFromRoom(r->pRoom, &n) : DUNGEON_GetWallTilesFromRoom(r->pRoom, &n);
				for (int i = 0; i < n; ++i) {
					if (!t[i].pTile) ++nNoGfx;
					(pass ? nWalls : nFloors)++;
					int tx = (t[i].nPosX + r->nTileXPos) * 5 - x0, ty = (t[i].nPosY + r->nTileYPos) * 5 - y0;
					for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
						int ix = tx + x, iy = ty + y;
						if (ix < 0 || iy < 0 || ix >= w || iy >= h) continue;
						unsigned char* p = &rgb[((size_t)iy * w + ix) * 3];
						if (pass == 0) { p[1] = 110; } else { p[2] = 200; p[1] = p[1] / 2; }
					}
				}
			}
		}
		for (size_t i = 0; i < img.size(); ++i) if (img[i] == 0) { rgb[i * 3] = 255; }
		FILE* fc = fopen("/data/local/tmp/tiles.ppm", "wb");
		fprintf(fc, "P6 %d %d 255\n", w, h);
		fwrite(rgb.data(), 1, rgb.size(), fc);
		fclose(fc);
		printf("tiles: %d floors, %d walls, %d without graphics\n", nFloors, nWalls, nNoGfx);
		FILE* f = fopen("/data/local/tmp/collision.pgm", "wb");
		fprintf(f, "P5 %d %d 255\n", w, h);
		fwrite(img.data(), 1, img.size(), f);
		fclose(f);
		printf("collision level %d: %d rooms, %d grids, %dx%d subtiles\n", nDump, nRooms, nGrids, w, h);
	}
	if (getenv("D2DIRS")) {
		const int v[8][2] = {{10, 0}, {10, 10}, {0, 10}, {-10, 10}, {-10, 0}, {-10, -10}, {0, -10}, {10, -10}};
		for (auto& p : v) printf("dir to %3d,%3d = %d\n", p[0], p[1], PATH_ComputeDirection(100, 100, 100 + p[0], 100 + p[1]));
	}
	if (getenv("D2MOVE")) {
		// Player movement through D2Common: spawn in town, walk/run 20 subtiles east, 2 s of frames.
		if (d2game_spawn(pAct, 4)) {
			int s[7];
			d2game_player_state(s);
			printf("spawn %d,%d mode %d level %d\n", s[0], s[1], s[4], s[6]);
			d2game_set_run(getenv("D2RUN") != nullptr);
			bool ok = d2game_move_to(s[0] + 20, s[1]);
			printf("path: %d points\n", ok ? PATH_GetNumberOfPathPoints(d2game_player()->pDynamicPath) : 0);
			for (int f = 1; f <= 50; ++f) {
				d2game_tick();
				d2game_player_state(s);
				if (f % 5 == 0 || f == 1) printf("frame %2d: %d,%d prec %.3f,%.3f mode %d dir %d\n", f, s[0], s[1], s[2] / 65536.0, s[3] / 65536.0, s[4], s[5]);
			}
		}
		d2game_free();
	}
	DUNGEON_FreeAct(pAct);
	return 0;
}
