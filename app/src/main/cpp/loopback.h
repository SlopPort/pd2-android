#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

void loopback_start();
// list 0: system messages (new game, join act...), 1: in-game commands.
void loopback_send(int list, int32_t nClientId, const void* pPacket, size_t nSize);
bool loopback_next_packet(std::vector<uint8_t>& out);
int32_t loopback_game_guid(int32_t nClientId);
