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
#include <strings.h>

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
	const size_t kUnitInts = 13;
	out.clear();
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* me = player(pGame);
		for (int list = 0; list < 5; ++list)
			for (int b = 0; b < 128; ++b)
				for (D2UnitStrc* u = pGame->pUnitList[list][b]; u; u = u->pListNext) {
					if (u == me || u->dwUnitType == UNIT_MISSILE) continue;
					D2CoordStrc c = {};
					UNITS_GetCoords(u, &c);
					// pDynamicPath and pStaticPath share one union: objects and items have a static path.
					const bool dynamic = u->pDynamicPath && u->dwUnitType != UNIT_OBJECT && u->dwUnitType != UNIT_ITEM;
					int dir = dynamic || (u->pStaticPath && (u->dwUnitType == UNIT_OBJECT || u->dwUnitType == UNIT_ITEM)) ? UNITS_GetDirection(u) : 0;
					int frame = 0, frames = 0;
					UNITS_GetFrameMetrics(u, &frame, &frames);
					out.push_back((int)u->dwUnitType); out.push_back(u->dwClassId); out.push_back((int)u->dwUnitId);
					out.push_back(c.nX); out.push_back(c.nY); out.push_back((int)u->dwAnimMode); out.push_back(dir); out.push_back(frame);
					// Sub-subtile position (16.16) for drawing, as for the hero; static units sit on their subtile.
					out.push_back(dynamic ? PATH_GetPrecisionX(u->pDynamicPath) : (c.nX << 16) + 0x8000); // centre of the subtile
					out.push_back(dynamic ? PATH_GetPrecisionY(u->pDynamicPath) : (c.nY << 16) + 0x8000);
					// For client-side animation (D2Client advances these itself): the direction the
					// unit is turning to, and the animation rate and length, both 8.8 fixed point.
					out.push_back(dynamic ? PATH_GetNewDirection(u->pDynamicPath) : dir);
					out.push_back(u->wAnimSpeed);
					out.push_back(u->dwFrameCountPrecise);
				}
	});
	return (int)out.size() / kUnitInts;
}

#include <D2Composit.h>
#include <D2Monsters.h>
#include <UNIT/SUnit.h>
#include <Units/Monster.h>
#include <D2Items.h>
#include <D2Inventory.h>

// What D2Client would load for a unit: COF folder/token/mode/weapon class from D2Common's composit
// code, and the component code of each layer (player: light armour; monster: its own picks).
bool sgame_unit_look(int nType, int nGuid, std::string& category, std::string& token, std::string& mode, std::string& wclass, std::string* armor) {
	bool ok = false;
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* u = nType == UNIT_PLAYER && nGuid == 0 ? player(pGame) : SUNIT_GetServerUnit(pGame, nType, nGuid);
		if (!u) return;
		char szPath[MAX_PATH] = {};
		int nWeaponClass = 0;
		D2Common_10884_COMPOSIT_unk(u, u->dwClassId, u->dwAnimMode, u->dwUnitType, u->pInventory, szPath, &nWeaponClass, TRUE, 1);
		// "DATA\GLOBAL\<CATEGORY>\<TK>\COF\<TK><MD><WCL>.COF"
		std::string p = szPath;
		static int logged;
		if (logged++ < 20) __android_log_print(ANDROID_LOG_INFO, "D2Native", "look type %d class %d mode %d: %s wclass %08x", nType, (int)u->dwClassId, (int)u->dwAnimMode, szPath, nWeaponClass);
		std::vector<std::string> parts;
		size_t start = 0;
		for (size_t i = 0; i <= p.size(); ++i) if (i == p.size() || p[i] == '\\') { parts.push_back(p.substr(start, i - start)); start = i + 1; }
		if (parts.size() < 6) return;
		category = parts[2]; token = parts[3];
		std::string file = parts[5];
		if (file.size() < token.size() + 2 + 4) return;
		mode = file.substr(token.size(), 2);
		wclass = file.substr(token.size() + 2, file.size() - token.size() - 2 - 4);
		for (int c = 0; c < 16; ++c) armor[c].clear();
		if (u->dwUnitType == UNIT_MONSTER && u->pMonsterData) {
			for (int c = 0; c < 16; ++c) {
				if (!D2COMMON_11068_GetCompInfo(u, c)) continue;
				uint32_t code = D2Common_11069(u, c, u->pMonsterData->nComponent[c]); // (unit, component, choice)
				char s[5] = {0};
				memcpy(s, &code, 4);
				for (int k = 3; k >= 0 && (s[k] == ' ' || !s[k]); --k) s[k] = 0;
				armor[c] = strcasecmp(s, "nil") ? s : ""; // "nil": no graphics for this layer
			}
		} else {
			// Body layers in the light set; weapon/shield layers use the code of the item held (e.g. HAX, BUC).
			for (int c = 0; c < 16; ++c) armor[c] = c <= 4 ? "LIT" : "";
			if (u->dwUnitType == UNIT_OBJECT) for (int c = 0; c < 16; ++c) armor[c] = "LIT";
			if (u->dwUnitType == UNIT_PLAYER && u->pInventory) {
				for (int loc : {BODYLOC_RARM, BODYLOC_LARM}) {
					D2UnitStrc* item = INVENTORY_GetItemFromBodyLoc(u->pInventory, loc);
					if (!item) continue;
					int comp = ITEMS_GetComponent(item);
					uint32_t code = ITEMS_GetBaseCode(item);
					char s[5] = {0};
					memcpy(s, &code, 4);
					for (int k = 3; k >= 0 && (s[k] == ' ' || !s[k]); --k) s[k] = 0;
					if (comp < 16) armor[comp] = s;
				}
			}
		}
		ok = true;
	});
	return ok;
}

#include <D2StatList.h>

int sgame_player_life() {
	int life = -1;
	withGame([&](D2GameStrc* pGame) {
		if (D2UnitStrc* u = player(pGame)) life = STATLIST_UnitGetStatValue(u, STAT_HITPOINTS, 0) >> 8;
	});
	return life;
}

// Diagnostics: a unit's key combat stats {level, tohit, mindamage, maxdamage, armorclass, life}.
void sgame_unit_combat_stats(int nType, int nGuid, int* out) {
	for (int i = 0; i < 6; ++i) out[i] = -1;
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* u = nType == UNIT_PLAYER && nGuid == 0 ? player(pGame) : SUNIT_GetServerUnit(pGame, nType, nGuid);
		if (!u) return;
		out[0] = STATLIST_UnitGetStatValue(u, STAT_LEVEL, 0);
		out[1] = STATLIST_UnitGetStatValue(u, STAT_TOHIT, 0);
		out[2] = STATLIST_UnitGetStatValue(u, STAT_MINDAMAGE, 0);
		out[3] = STATLIST_UnitGetStatValue(u, STAT_MAXDAMAGE, 0);
		out[4] = STATLIST_UnitGetStatValue(u, STAT_ARMORCLASS, 0);
		out[5] = STATLIST_UnitGetStatValue(u, STAT_HITPOINTS, 0) >> 8;
	});
}

// Fixed game seed for repeatable tests (D2Game's own switch; -1 = random as in normal play).
void sgame_set_seed(int nSeed) { GAME_SetInitSeed(nSeed); }

// Left skill on a unit (packet 0x06), as the client sends when the player clicks a monster.
void sgame_attack(int nType, int nGuid) {
	if (!sgame_ready()) return;
	D2GSPacketClt06 p = {};
	p.nHeader = 0x06;
	p.dwUnitType = (uint32_t)nType; p.dwUnitGUID = (uint32_t)nGuid;
	loopback_send(1, kClientId, &p, sizeof p);
}

#include <DataTbls/ItemsTbls.h>

// Ground graphics of an item lying on the floor: its items.txt flippy DC6 ("" when not on the ground).
std::string sgame_ground_item_file(int nGuid) {
	std::string file;
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* u = SUNIT_GetServerUnit(pGame, UNIT_ITEM, nGuid);
		if (!u || (u->dwAnimMode != IMODE_ONGROUND && u->dwAnimMode != IMODE_DROPPING)) return;
		D2ItemsTxt* r = DATATBLS_GetItemsTxtRecord(u->dwClassId);
		if (!r || !r->szFlippyFile[0]) return;
		file = std::string("DATA\\GLOBAL\\ITEMS\\") + r->szFlippyFile + ".DC6";
	});
	return file;
}

// Hero {life, max life, mana, max mana} in points (stats are 8.8 fixed point).
void sgame_player_vitals(int* out) {
	for (int i = 0; i < 4; ++i) out[i] = 0;
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* u = player(pGame);
		if (!u) return;
		out[0] = STATLIST_UnitGetStatValue(u, STAT_HITPOINTS, 0) >> 8;
		out[1] = STATLIST_GetMaxLifeFromUnit(u) >> 8;
		out[2] = STATLIST_UnitGetStatValue(u, STAT_MANA, 0) >> 8;
		out[3] = STATLIST_GetMaxManaFromUnit(u) >> 8;
	});
}

// Interact with a unit (packet 0x13), as a click on an item, NPC or object: D2Game walks the hero
// there if needed, then picks up / talks / operates.
void sgame_interact(int nType, int nGuid) {
	if (!sgame_ready()) return;
	D2GSPacketClt13 p = {};
	p.nHeader = 0x13;
	p.dwUnitType = (uint32_t)nType; p.dwUnitGUID = (uint32_t)nGuid;
	loopback_send(1, kClientId, &p, sizeof p);
}

// Diagnostics: gold carried by the hero and number of items in his inventory.
void sgame_player_goods(int* out) {
	out[0] = out[1] = -1;
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* u = player(pGame);
		if (!u) return;
		out[0] = STATLIST_UnitGetStatValue(u, STAT_GOLD, 0);
		out[1] = 0;
		if (u->pInventory) for (D2UnitStrc* i = INVENTORY_GetFirstItem(u->pInventory); i; i = INVENTORY_GetNextItem(i)) ++out[1];
	});
}

// After death: the client's "press Esc to continue" (packet 0x41); D2Game revives the hero in town.
void sgame_resurrect() {
	D2GSPacketClt41 p = {0x41};
	loopback_send(1, kClientId, &p, sizeof p);
}

// Test only: set the hero's current life (points).
void sgame_debug_set_life(int nLife) {
	withGame([&](D2GameStrc* pGame) {
		if (D2UnitStrc* u = player(pGame)) STATLIST_SetUnitStat(u, STAT_HITPOINTS, nLife << 8, 0);
	});
}

// The hero's items as the inventory panel needs them: {invfile, page, body location, x, y, w, h}
// per item. x/y are grid cells (the item's static path in inventories).
void sgame_inventory(std::vector<InvItem>& out) {
	out.clear();
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* u = player(pGame);
		if (!u || !u->pInventory) return;
		for (D2UnitStrc* i = INVENTORY_GetFirstItem(u->pInventory); i; i = INVENTORY_GetNextItem(i)) {
			D2ItemsTxt* r = DATATBLS_GetItemsTxtRecord(i->dwClassId);
			if (!r) continue;
			InvItem it;
			it.file = std::string("DATA\\GLOBAL\\ITEMS\\") + r->szInvFile + ".DC6";
			it.page = ITEMS_GetInvPage(i);
			it.bodyLoc = ITEMS_GetBodyLocation(i);
			D2CoordStrc c = {};
			UNITS_GetCoords(i, &c);
			it.x = c.nX; it.y = c.nY;
			it.w = r->nInvWidth; it.h = r->nInvHeight;
			it.guid = (int)i->dwUnitId; it.mode = (int)i->dwAnimMode; // IMODE_ONCURSOR (4): on the cursor
			out.push_back(it);
		}
		// The cursor item is kept apart from the item list.
		D2UnitStrc* pCursor = INVENTORY_GetCursorItem(u->pInventory);
		if (pCursor) {
			bool listed = false;
			for (const InvItem& it : out) listed |= it.guid == (int)pCursor->dwUnitId;
			D2ItemsTxt* r = DATATBLS_GetItemsTxtRecord(pCursor->dwClassId);
			if (!listed && r) {
				InvItem it;
				it.file = std::string("DATA\\GLOBAL\\ITEMS\\") + r->szInvFile + ".DC6";
				it.page = 255; it.bodyLoc = 0; it.x = it.y = 0;
				it.w = r->nInvWidth; it.h = r->nInvHeight;
				it.guid = (int)pCursor->dwUnitId; it.mode = IMODE_ONCURSOR;
				out.push_back(it);
			}
		}
	});
}

#include <DataTbls/InvTbls.h>

// The hero's inventory panel in 800x600 from inventory.txt: panel rect (4), grid {cols, rows, left,
// right, top, bottom, box w, box h} (8), then per body component {left, right, top, bottom, w, h} (10 x 6).
bool sgame_inventory_layout(int* out) {
	bool ok = false;
	withGame([&](D2GameStrc* pGame) {
		D2UnitStrc* u = player(pGame);
		if (!u) return;
		const int id = UNITS_GetInventoryRecordId(u, INVPAGE_INVENTORY, TRUE);
		D2InvRectStrc r = {};
		DATATBLS_GetInventoryRect(id, 1, &r);
		out[0] = r.nLeft; out[1] = r.nRight; out[2] = r.nTop; out[3] = r.nBottom;
		D2InventoryGridInfoStrc g = {};
		DATATBLS_GetInventoryGridInfo(id, 1, &g);
		int* o = out + 4;
		o[0] = g.nGridX; o[1] = g.nGridY; o[2] = g.nGridLeft; o[3] = g.nGridRight; o[4] = g.nGridTop; o[5] = g.nGridBottom; o[6] = g.nGridBoxWidth; o[7] = g.nGridBoxHeight;
		for (int c = 0; c < NUM_INV_COMPS; ++c) {
			D2InvCompGridStrc cg = {};
			DATATBLS_GetInventoryComponentGrid(id, 1, &cg, c);
			int* p = out + 12 + c * 6;
			p[0] = cg.rect.nLeft; p[1] = cg.rect.nRight; p[2] = cg.rect.nTop; p[3] = cg.rect.nBottom; p[4] = cg.nWidth; p[5] = cg.nHeight;
		}
		ok = true;
	});
	return ok;
}

// Item moves as the client sends them (packet layouts from D2Game's handlers in PlrMsg.cpp):
// 0x19 take from a page to the cursor, 0x18 put the cursor item at x,y of a page, 0x1A equip the
// cursor item, 0x1D swap it with the equipped one, 0x1C take an equipped item to the cursor,
// 0x17 drop the cursor item on the ground, 0x26 use (drink) an item from the belt.
void sgame_item_command(int nCommand, int nGuid, int a, int b, int c) {
	if (!sgame_ready()) return;
	uint8_t p[17] = {(uint8_t)nCommand};
	size_t n = 0;
	auto put32 = [&](size_t at, int v) { memcpy(p + at, &v, 4); };
	switch (nCommand) {
	case 0x17: case 0x19: put32(1, nGuid); n = 5; break;
	case 0x18: put32(1, nGuid); put32(5, a); put32(9, b); put32(13, c); n = 17; break;
	case 0x1A: case 0x1D: put32(1, nGuid); put32(5, a); n = 9; break;
	case 0x26: put32(1, nGuid); put32(5, a); put32(9, 0); n = 13; break; // use belt item (a: on mercenary)
	case 0x1C: { uint16_t loc = (uint16_t)a; memcpy(p + 1, &loc, 2); n = 3; break; }
	default: return;
	}
	loopback_send(1, kClientId, p, n);
}
