// Minimal game state on top of D2Common: one player unit that moves exactly like D2Game moves a
// player (path computed by D2Common_10142, one frame of movement by D2Common_10226, 25 frames/s).
// What D2Game does around it (combat, events, stats, stamina) is not here yet.
#include "game.h"
#include <D2Dungeon.h>
#include <D2Collision.h>
#include <D2StatList.h>
#include <Path/Path.h>
#include <Path/PathMisc.h>
#include <Units/Units.h>
#include <Drlg/D2DrlgActivate.h>
#include <D2Constants.h>
#include <D2DataTbls.h>
#include <android/log.h>
#include <cstdlib>

namespace {
D2DrlgActStrc* gAct;
D2UnitStrc* gPlayer;
D2PlayerDataStrc* gPlayerData;
bool gRun;
}

bool d2game_spawn(D2DrlgActStrc* pAct, int nClass) {
	d2game_free();
	gAct = pAct;
	gPlayer = (D2UnitStrc*)calloc(1, sizeof(D2UnitStrc));
	gPlayerData = (D2PlayerDataStrc*)calloc(1, sizeof(D2PlayerDataStrc));
	gPlayer->dwUnitType = UNIT_PLAYER;
	gPlayer->dwClassId = nClass;
	gPlayer->dwUnitId = 1;
	gPlayer->pPlayerData = gPlayerData;
	gPlayer->nAct = pAct->nAct;
	gPlayer->pDrlgAct = pAct;

	// As D2Game PLAYER_Create + PLAYERSTATS new character: stat list and starting stats from charstats.
	STATLIST_AllocStatListEx(gPlayer, 0, nullptr, nullptr);
	if (D2CharStatsTxt* cs = UNITS_GetCharStatsTxtRecord(nClass)) {
		STATLIST_SetUnitStat(gPlayer, STAT_STRENGTH, cs->nStr, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_ENERGY, cs->nInt, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_DEXTERITY, cs->nDex, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_VITALITY, cs->nVit, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_HITPOINTS, (cs->nVit + cs->nLifeAdd) << 8, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_MAXHP, (cs->nVit + cs->nLifeAdd) << 8, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_MANA, cs->nInt << 8, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_MAXMANA, cs->nInt << 8, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_STAMINA, cs->nStamina << 8, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_MAXSTAMINA, cs->nStamina << 8, 0);
		STATLIST_SetUnitStat(gPlayer, STAT_LEVEL, 1, 0);
	}
	STATLIST_SetUnitStat(gPlayer, STAT_ATTACKRATE, 100, 0);
	STATLIST_SetUnitStat(gPlayer, STAT_VELOCITYPERCENT, 100, 0);
	STATLIST_SetUnitStat(gPlayer, STAT_OTHER_ANIMRATE, 100, 0);

	// As D2Game.Clients.cpp: town spawn location found for the player's size.
	int nX = 0, nY = 0;
	int nTown = DUNGEON_GetTownLevelIdFromAct(pAct);
	D2ActiveRoomStrc* pRoom = DUNGEON_FindActSpawnLocationEx(pAct, nTown, 0, &nX, &nY, UNITS_GetUnitSizeX(gPlayer));
	if (!pRoom) {
		__android_log_print(ANDROID_LOG_ERROR, "D2Native", "no spawn location in level %d", nTown);
		return false;
	}
	PATH_AllocDynamicPath(nullptr, pRoom, nX, nY, gPlayer, TRUE);
	UNITS_ChangeAnimMode(gPlayer, PLRMODE_TOWNNEUTRAL);
	__android_log_print(ANDROID_LOG_INFO, "D2Native", "player spawned at %d,%d level %d", nX, nY, nTown);
	return true;
}

void d2game_free() {
	if (gPlayer) {
		if (gPlayer->pDynamicPath) PATH_FreeDynamicPath(nullptr, gPlayer->pDynamicPath);
		free(gPlayer);
		free(gPlayerData);
	}
	gPlayer = nullptr; gPlayerData = nullptr;
}

void d2game_set_run(bool bRun) { gRun = bRun; }

// Player clicked/held towards a subtile: what PLRMODE_StartXY_WalkRunKnockback does.
bool d2game_move_to(int nX, int nY) {
	if (!gPlayer) return false;
	D2ActiveRoomStrc* pRoom = UNITS_GetRoom(gPlayer);
	bool bTown = pRoom && DUNGEON_IsRoomInTown(pRoom);
	int nMode = gRun ? PLRMODE_RUN : (bTown ? PLRMODE_TOWNWALK : PLRMODE_WALK);
	D2COMMON_10170_PathSetTargetPos(gPlayer->pDynamicPath, nX, nY);
	PATH_ResetToPreviousType(gPlayer->pDynamicPath);
	D2Common_10142(gPlayer->pDynamicPath, gPlayer, 0);
	if (PATH_GetNumberOfPathPoints(gPlayer->pDynamicPath)) {
		UNITS_ChangeAnimMode(gPlayer, nMode);
		return true;
	}
	UNITS_ChangeAnimMode(gPlayer, bTown ? PLRMODE_TOWNNEUTRAL : PLRMODE_NEUTRAL);
	return false;
}

// One game frame (1/25 s).
void d2game_tick() {
	if (!gPlayer || !gPlayer->pDynamicPath) return;
	UNITS_UpdateFrame(gPlayer); // animation advances every game frame, as in D2Game
	D2COMMON_10193_PATH_AdjustDirection(gPlayer->pDynamicPath); // turn towards the path direction
	int nMode = gPlayer->dwAnimMode;
	if (nMode == PLRMODE_WALK || nMode == PLRMODE_TOWNWALK || nMode == PLRMODE_RUN) {
		if (!D2Common_10226(gPlayer, 1024)) {
			D2ActiveRoomStrc* pRoom = UNITS_GetRoom(gPlayer);
			UNITS_ChangeAnimMode(gPlayer, pRoom && DUNGEON_IsRoomInTown(pRoom) ? PLRMODE_TOWNNEUTRAL : PLRMODE_NEUTRAL);
		}
	}
}

// {subtile x, subtile y, precision x, precision y (16.16), mode, direction, level, frame, frame count}
void d2game_player_state(int* out) {
	if (!gPlayer || !gPlayer->pDynamicPath) { for (int i = 0; i < 9; ++i) out[i] = 0; return; }
	D2DynamicPathStrc* p = gPlayer->pDynamicPath;
	out[0] = PATH_GetXPosition(p); out[1] = PATH_GetYPosition(p);
	out[2] = PATH_GetPrecisionX(p); out[3] = PATH_GetPrecisionY(p);
	out[4] = gPlayer->dwAnimMode; out[5] = PATH_GetDirection(p);
	out[6] = DUNGEON_GetLevelIdFromRoom(UNITS_GetRoom(gPlayer));
	UNITS_GetFrameMetrics(gPlayer, &out[7], &out[8]);
}

D2UnitStrc* d2game_player() { return gPlayer; }
