// In-process "network" between the app (as D2Client) and D2Game, like D2 1.10's single player:
// client packets go into D2Net's message lists, which D2Game reads in GAME_ProcessNetworkMessages,
// and the server's packets for the client are collected here.
#include "loopback.h"
#include <Windows.h>
#include <D2BasicTypes.h>
#include <D2PacketDef.h>
#include <Server.h>
#include <GAME/Game.h>
#include <GAME/Clients.h>
#include <android/log.h>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace {
std::mutex gLock;
std::deque<std::vector<uint8_t>> gLists[3]; // 0 system, 1 game, 2 other
std::map<int32_t, int32_t> gClientGame;     // client id -> game GUID
std::deque<std::vector<uint8_t>> gToClient;
D2GameDataTableStrc* gGameTable;
bool gStarted;

int32_t readList(int n, uint8_t* pBuffer, int32_t nBufferSize) {
	std::lock_guard<std::mutex> l(gLock);
	if (gLists[n].empty()) return -1;
	std::vector<uint8_t> m = std::move(gLists[n].front());
	gLists[n].pop_front();
	int32_t size = (int32_t)std::min<size_t>(m.size(), (size_t)nBufferSize);
	memcpy(pBuffer, m.data(), size);
	// The buffer holds [client id][packet]; D2Net reports the size of the packet itself.
	return size - 4;
}
}

int32_t __stdcall SERVER_ReadFromMessageList0(uint8_t* pBuffer, int32_t nBufferSize) { return readList(0, pBuffer, nBufferSize); }
int32_t __stdcall SERVER_ReadFromMessageList1(uint8_t* pBuffer, int32_t nBufferSize) { return readList(1, pBuffer, nBufferSize); }
int32_t __stdcall SERVER_ReadFromMessageList2(uint8_t* pBuffer, int32_t nBufferSize) { return readList(2, pBuffer, nBufferSize); }
int32_t __stdcall SERVER_SetClientGameGUID(int32_t nClientId, int32_t dwGameGuid) { std::lock_guard<std::mutex> l(gLock); gClientGame[nClientId] = dwGameGuid; return 1; }
int32_t __stdcall SERVER_GetClientGameGUID(int32_t nClientId) { std::lock_guard<std::mutex> l(gLock); auto it = gClientGame.find(nClientId); return it == gClientGame.end() ? 0 : it->second; }

// Server -> client packet.
uint32_t __stdcall D2NET_10006(int8_t, int32_t, void* pBuffer, uint32_t nBufferSize) {
	std::lock_guard<std::mutex> l(gLock);
	if (gToClient.size() < 4096) gToClient.emplace_back((uint8_t*)pBuffer, (uint8_t*)pBuffer + nBufferSize);
	return nBufferSize;
}

void loopback_send(int list, int32_t nClientId, const void* pPacket, size_t nSize) {
	std::vector<uint8_t> m(4 + nSize);
	memcpy(m.data(), &nClientId, 4);
	memcpy(m.data() + 4, pPacket, nSize);
	std::lock_guard<std::mutex> l(gLock);
	gLists[list].push_back(std::move(m));
}

bool loopback_next_packet(std::vector<uint8_t>& out) {
	std::lock_guard<std::mutex> l(gLock);
	if (gToClient.empty()) return false;
	out = std::move(gToClient.front());
	gToClient.pop_front();
	return true;
}

// D2Game start-up as the single-player client did it: server init, game table.
void loopback_start() {
	if (gStarted) return;
	gStarted = true;
	D2Game_10046();
	gGameTable = new D2GameDataTableStrc();
	static int gameList; // only checked for non-null by D2Game
	GAME_InitGameDataTable(gGameTable, &gameList);
}

int32_t loopback_game_guid(int32_t nClientId) { return SERVER_GetClientGameGUID(nClientId); }
