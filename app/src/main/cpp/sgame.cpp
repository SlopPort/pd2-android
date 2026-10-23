// The app's side of a D2Game single-player game: create/join as the 1.10 client did, run server
// frames, send commands as client packets, and read the server's units for drawing.
#include "sgame.h"
#include "loopback.h"
#include <Windows.h>
#include <D2BasicTypes.h>
#include <D2PacketDef.h>
#include <GAME/Game.h>
#include <GAME/Clients.h>
#include <GAME/CCmd.h>
#include <D2Dungeon.h>
#include <Path/Path.h>
#include <Units/Units.h>
#include <android/log.h>
#include <cstring>

namespace {
const int32_t kClientId = 1;

// Runs fn with the game locked; false when there is no game.
template<class F> bool withGame(F fn) {
	int32_t guid = loopback_game_guid(kClientId);
	if (!guid) return false;
	D2GameStrc* pGame = GAME_LockGame(guid);
	static int logged;
	if (!pGame && logged++ < 3) __android_log_print(ANDROID_LOG_WARN, "D2Native", "game %d not found", guid);
	if (!pGame) return false;
	fn(pGame);
	LeaveCriticalSection(pGame->lpCriticalSection);
	return true;
}

D2UnitStrc* player(D2GameStrc* pGame) {
	D2ClientStrc* pClient = CLIENTS_GetClientFromClientId(pGame, kClientId);
	static int logged;
	if (!pClient || !pClient->pPlayer) {
		if (logged++ < 3) __android_log_print(ANDROID_LOG_WARN, "D2Native", "player lookup: client %p state %d player %p", (void*)pClient, pClient ? (int)pClient->dwClientState : -1, pClient ? (void*)pClient->pPlayer : nullptr);
		return nullptr;
	}
	return pClient->pPlayer;
}
}

bool sgame_create(const char* szName, int nClass, int nDifficulty) {
	loopback_start();
	D2GSPacketClt66 p = {};
	p.nHeader = D2CLTSYS_NEWGAME;
	strncpy(p.szGameName, "android", sizeof p.szGameName - 1);
	p.nGameType = 0;            // single player
	p.nPlayerClass = (uint8_t)nClass;
	p.nTemplate = 0;
	p.nDifficulty = (int8_t)nDifficulty;
	strncpy(p.szClientName, szName, sizeof p.szClientName - 1);
	p.nGameFlags = 0x4 | 0x100000; // single player, expansion
	p.nLocale = 0;
	loopback_send(0, kClientId, &p, sizeof p);
	GAME_ProcessNetworkMessages();
	if (!loopback_game_guid(kClientId)) { __android_log_print(ANDROID_LOG_ERROR, "D2Native", "new game refused"); return false; }

	D2GSPacketClt6A join = {D2CLTSYS_JOINACT};
	loopback_send(0, kClientId, &join, sizeof join);
	GAME_ProcessNetworkMessages();
	bool ok = false;
	withGame([&](D2GameStrc* pGame) { ok = player(pGame) != nullptr; });
	__android_log_print(ANDROID_LOG_INFO, "D2Native", "game created, player %s", ok ? "in game" : "MISSING");
	return ok;
}

void sgame_tick() {
	GAME_ProcessNetworkMessages();
	GAME_UpdateGamesProgress(0);
	std::vector<uint8_t> packet;
	while (loopback_next_packet(packet)) {} // the app reads server state directly
}

bool sgame_ready() {
	bool ready = false;
	withGame([&](D2GameStrc* pGame) {
		D2ClientStrc* pClient = CLIENTS_GetClientFromClientId(pGame, kClientId);
		ready = pClient && pClient->dwClientState == CLIENTSTATE_INGAME;
	});
	return ready;
}

void sgame_move(int nX, int nY, bool bRun) {
	// Commands before the client is in game count as cheating in D2Game (the real client waits too).
	if (!sgame_ready()) return;
	D2GSPacketClt01 p = {};
	p.nHeader = bRun ? 0x03 : 0x01;
	p.nPosX = (uint16_t)nX; p.nPosY = (uint16_t)nY;
	loopback_send(1, kClientId, &p, sizeof p);
}

D2DrlgActStrc* sgame_act() {
	D2DrlgActStrc* pAct = nullptr;
	withGame([&](D2GameStrc* pGame) {
		if (D2UnitStrc* pPlayer = player(pGame)) pAct = pGame->pAct[pPlayer->nAct];
	});
	return pAct;
}

void sgame_player_state(int* out) {
	for (int i = 0; i < 9; ++i) out[i] = 0;
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* u = player(pGame);
		if (!u || !u->pDynamicPath) return;
		D2DynamicPathStrc* p = u->pDynamicPath;
		out[0] = PATH_GetXPosition(p); out[1] = PATH_GetYPosition(p);
		out[2] = PATH_GetPrecisionX(p); out[3] = PATH_GetPrecisionY(p);
		out[4] = u->dwAnimMode; out[5] = PATH_GetDirection(p);
		out[6] = DUNGEON_GetLevelIdFromRoom(UNITS_GetRoom(u));
		UNITS_GetFrameMetrics(u, &out[7], &out[8]);
	});
}

int sgame_units(std::vector<int>& out) {
	out.clear();
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* me = player(pGame);
		for (int list = 0; list < 5; ++list)
			for (int b = 0; b < 128; ++b)
				for (D2UnitStrc* u = pGame->pUnitList[list][b]; u; u = u->pListNext) {
					if (u == me || u->dwUnitType == UNIT_MISSILE) continue;
					D2CoordStrc c = {};
					UNITS_GetCoords(u, &c);
					int dir = u->pDynamicPath ? PATH_GetDirection(u->pDynamicPath) : 0;
					int frame = 0, frames = 0;
					UNITS_GetFrameMetrics(u, &frame, &frames);
					out.push_back((int)u->dwUnitType); out.push_back(u->dwClassId); out.push_back((int)u->dwUnitId);
					out.push_back(c.nX); out.push_back(c.nY); out.push_back((int)u->dwAnimMode); out.push_back(dir); out.push_back(frame);
				}
	});
	return (int)out.size() / 8;
}
